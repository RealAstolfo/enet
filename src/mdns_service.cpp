#include "mdns_service.hpp"

// The mjansson/mdns implementation is header-only (static functions), so
// including it here compiles it into this one translation unit.
#define MDNS_IMPLEMENTATION
#include <mdns.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace enet::mdns {
namespace {

constexpr std::size_t kMaxLabel = 63;
constexpr long kAddrRefreshMs = 30000;
constexpr std::size_t kMaxTxt = 16;

struct iface_v4 {
	struct in_addr addr {};
	struct in_addr mask {};
};

std::string hostname_label() {
	char host[256] = {0};
	if (::gethostname(host, sizeof(host) - 1) == 0 && host[0] != '\0') {
		host[sizeof(host) - 1] = '\0';
		return host;
	}
	return "phobos";
}

std::string lower(std::string s) {
	std::ranges::transform(s, s.begin(), [](char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	});
	return s;
}

std::string sanitize_label(std::string s) {
	std::ranges::replace(s, '.', '-');
	if (s.size() > kMaxLabel)
		s.resize(kMaxLabel);
	return s;
}

std::vector<iface_v4> local_ipv4() {
	std::vector<iface_v4> out;
	struct ifaddrs* ifaddr = nullptr;
	if (::getifaddrs(&ifaddr) != 0 || ifaddr == nullptr)
		return out;
	for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		iface_v4 entry;
		struct sockaddr_in sa {};
		std::memcpy(&sa, ifa->ifa_addr, sizeof(sa));
		if ((ntohl(sa.sin_addr.s_addr) >> 24) == 127U)
			continue;
		entry.addr = sa.sin_addr;
		if (ifa->ifa_netmask != nullptr) {
			struct sockaddr_in mk {};
			std::memcpy(&mk, ifa->ifa_netmask, sizeof(mk));
			entry.mask = mk.sin_addr;
		}
		const bool dup = std::ranges::any_of(
		    out, [&](const iface_v4& o) { return o.addr.s_addr == entry.addr.s_addr; });
		if (!dup)
			out.push_back(entry);
	}
	::freeifaddrs(ifaddr);
	return out;
}

mdns_string_t to_mdns_string(const std::string& s) {
	mdns_string_t out;
	out.str = s.c_str();
	out.length = s.length();
	return out;
}

long now_ms() {
	struct timespec ts {};
	::clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<long>(ts.tv_sec) * 1000L + static_cast<long>(ts.tv_nsec) / 1000000L;
}

struct service_records {
	std::string service_type;
	std::string service_instance;
	std::string hostname_qualified;
	std::uint16_t port = 0;
	std::vector<std::pair<std::string, std::string>> txt;
	mdns_record_t record_ptr{};
	mdns_record_t record_srv{};
	std::vector<mdns_record_t> record_txt;
	std::vector<mdns_record_t> record_a;
	long addrs_at = 0;
};

void build_records(service_records& svc) {
	const mdns_string_t type_str = to_mdns_string(svc.service_type);
	const mdns_string_t instance_str = to_mdns_string(svc.service_instance);
	const mdns_string_t host_str = to_mdns_string(svc.hostname_qualified);

	svc.record_ptr.name = type_str;
	svc.record_ptr.type = MDNS_RECORDTYPE_PTR;
	svc.record_ptr.data.ptr.name = instance_str;

	svc.record_srv.name = instance_str;
	svc.record_srv.type = MDNS_RECORDTYPE_SRV;
	svc.record_srv.data.srv.name = host_str;
	svc.record_srv.data.srv.port = svc.port;
	svc.record_srv.data.srv.priority = 0;
	svc.record_srv.data.srv.weight = 0;

	svc.record_txt.clear();
	for (const auto& [k, v] : svc.txt) {
		mdns_record_t rec{};
		rec.name = instance_str;
		rec.type = MDNS_RECORDTYPE_TXT;
		rec.data.txt.key = to_mdns_string(k);
		rec.data.txt.value = to_mdns_string(v);
		svc.record_txt.push_back(rec);
	}

	svc.record_a.clear();
	for (const auto& ifc : local_ipv4()) {
		mdns_record_t rec{};
		rec.name = host_str;
		rec.type = MDNS_RECORDTYPE_A;
		rec.data.a.addr.sin_family = AF_INET;
		rec.data.a.addr.sin_port = htons(svc.port);
		rec.data.a.addr.sin_addr = ifc.addr;
		svc.record_a.push_back(rec);
	}
	svc.addrs_at = now_ms();
}

std::vector<mdns_record_t> with_extras(const service_records& svc, bool srv, bool txt) {
	std::vector<mdns_record_t> out;
	if (srv)
		out.push_back(svc.record_srv);
	if (txt)
		out.insert(out.end(), svc.record_txt.begin(), svc.record_txt.end());
	out.insert(out.end(), svc.record_a.begin(), svc.record_a.end());
	return out;
}

bool name_is(const mdns_string_t& name, const std::string& want) {
	return name.length == want.length() &&
	       lower(std::string(name.str, name.length)) == lower(want);
}

int service_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
                     uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl,
                     const void* data, size_t size, size_t name_offset, size_t name_length,
                     size_t record_offset, size_t record_length, void* user_data) {
	static_cast<void>(ttl);
	static_cast<void>(name_length);
	static_cast<void>(record_offset);
	static_cast<void>(record_length);
	if (entry != MDNS_ENTRYTYPE_QUESTION)
		return 0;

	const auto* svc = static_cast<const service_records*>(user_data);
	if (svc == nullptr)
		return 0;

	static thread_local char namebuffer[256];
	static thread_local char sendbuffer[4096];

	size_t offset = name_offset;
	const mdns_string_t name =
	    mdns_string_extract(data, size, &offset, namebuffer, sizeof(namebuffer));

	const std::uint16_t unicast = static_cast<std::uint16_t>(rclass & MDNS_UNICAST_RESPONSE);

	const auto answer_query = [&](const mdns_record_t& answer,
	                              const std::vector<mdns_record_t>& additional) {
		if (unicast != 0) {
			static_cast<void>(mdns_query_answer_unicast(
			    sock, from, addrlen, sendbuffer, sizeof(sendbuffer), query_id,
			    static_cast<mdns_record_type_t>(rtype), name.str, name.length, answer, nullptr, 0,
			    additional.data(), additional.size()));
		} else {
			static_cast<void>(mdns_query_answer_multicast(sock, sendbuffer, sizeof(sendbuffer),
			                                              answer, nullptr, 0, additional.data(),
			                                              additional.size()));
		}
	};

	const bool any = rtype == MDNS_RECORDTYPE_ANY;
	if (name_is(name, svc->service_type)) {
		if (rtype == MDNS_RECORDTYPE_PTR || any)
			answer_query(svc->record_ptr, with_extras(*svc, true, true));
	} else if (name_is(name, svc->service_instance)) {
		if (rtype == MDNS_RECORDTYPE_SRV || any)
			answer_query(svc->record_srv, with_extras(*svc, false, true));
		else if (rtype == MDNS_RECORDTYPE_TXT && !svc->record_txt.empty())
			answer_query(svc->record_txt.front(), {});
	} else if (name_is(name, svc->hostname_qualified)) {
		if ((rtype == MDNS_RECORDTYPE_A || any) && !svc->record_a.empty()) {
			std::vector<mdns_record_t> rest(svc->record_a.begin() + 1, svc->record_a.end());
			answer_query(svc->record_a.front(), rest);
		}
	}
	return 0;
}

void responder_loop(service_records* svc) {
	struct sockaddr_in listen_addr {};
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	listen_addr.sin_port = htons(static_cast<std::uint16_t>(MDNS_PORT));

	const int sock = mdns_socket_open_ipv4(&listen_addr);
	if (sock < 0) {
		delete svc;
		return;
	}

	build_records(*svc);
	std::vector<unsigned char> buffer(4096);
	{
		const auto additional = with_extras(*svc, true, true);
		static_cast<void>(mdns_announce_multicast(sock, buffer.data(), buffer.size(),
		                                          svc->record_ptr, nullptr, 0, additional.data(),
		                                          additional.size()));
	}

	for (;;) {
		if (now_ms() - svc->addrs_at > kAddrRefreshMs)
			build_records(*svc);

		fd_set readfs;
		FD_ZERO(&readfs);
		FD_SET(sock, &readfs);

		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		const int res = ::select(sock + 1, &readfs, nullptr, nullptr, &timeout);
		if (res < 0)
			break;
		if (res > 0 && FD_ISSET(sock, &readfs)) {
			static_cast<void>(mdns_socket_listen(sock, buffer.data(), buffer.size(),
			                                     service_callback, svc));
		}
	}

	mdns_socket_close(sock);
	delete svc;
}

struct packet_state {
	std::string service_type;
	std::string source_ip;
	struct srv_rec {
		std::string instance;
		std::string target;
		std::uint16_t port = 0;
	};
	std::vector<srv_rec> srvs;
	std::map<std::string, std::map<std::string, std::string>> txts;
	std::map<std::string, std::vector<std::string>> addrs;
};

std::string sockaddr_ip(const struct sockaddr* sa) {
	char buf[INET6_ADDRSTRLEN] = {0};
	if (sa->sa_family == AF_INET) {
		struct sockaddr_in sin {};
		std::memcpy(&sin, sa, sizeof(sin));
		::inet_ntop(AF_INET, &sin.sin_addr, buf, sizeof(buf));
	}
	return buf;
}

int query_callback(int sock, const struct sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
                   uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl,
                   const void* data, size_t size, size_t name_offset, size_t name_length,
                   size_t record_offset, size_t record_length, void* user_data) {
	static_cast<void>(sock);
	static_cast<void>(addrlen);
	static_cast<void>(entry);
	static_cast<void>(query_id);
	static_cast<void>(rclass);
	static_cast<void>(ttl);
	static_cast<void>(name_length);

	auto* pkt = static_cast<packet_state*>(user_data);
	if (pkt == nullptr)
		return 0;
	if (pkt->source_ip.empty() && from != nullptr)
		pkt->source_ip = sockaddr_ip(from);

	static thread_local char namebuffer[256];
	static thread_local char valuebuffer[256];

	size_t offset = name_offset;
	const mdns_string_t rec_name =
	    mdns_string_extract(data, size, &offset, namebuffer, sizeof(namebuffer));
	const std::string name = lower(std::string(rec_name.str, rec_name.length));

	if (rtype == MDNS_RECORDTYPE_SRV) {
		if (!name.ends_with("." + pkt->service_type))
			return 0;
		const mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
		                                                    valuebuffer, sizeof(valuebuffer));
		pkt->srvs.push_back({name, lower(std::string(srv.name.str, srv.name.length)), srv.port});
	} else if (rtype == MDNS_RECORDTYPE_TXT) {
		mdns_record_txt_t txt[kMaxTxt];
		const size_t n = mdns_record_parse_txt(data, size, record_offset, record_length, txt, kMaxTxt);
		auto& kv = pkt->txts[name];
		for (size_t i = 0; i < n; ++i) {
			kv[std::string(txt[i].key.str, txt[i].key.length)] =
			    std::string(txt[i].value.str, txt[i].value.length);
		}
	} else if (rtype == MDNS_RECORDTYPE_A) {
		struct sockaddr_in addr {};
		if (mdns_record_parse_a(data, size, record_offset, record_length, &addr) != nullptr) {
			char ipbuf[INET_ADDRSTRLEN] = {0};
			if (::inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf)) != nullptr) {
				auto& list = pkt->addrs[name];
				if (std::ranges::find(list, std::string(ipbuf)) == list.end())
					list.emplace_back(ipbuf);
			}
		}
	}
	return 0;
}

void merge_packet(const packet_state& pkt, std::map<std::string, service_instance>& found) {
	for (const auto& srv : pkt.srvs) {
		auto& svc = found[srv.instance];
		svc.instance = srv.instance;
		svc.host = srv.target;
		svc.port = srv.port;
		svc.source_ip = pkt.source_ip;
		if (auto it = pkt.addrs.find(srv.target); it != pkt.addrs.end())
			svc.ipv4 = it->second;
		if (auto it = pkt.txts.find(srv.instance); it != pkt.txts.end())
			svc.txt = it->second;
	}
	for (const auto& [inst, kv] : pkt.txts) {
		if (auto it = found.find(inst); it != found.end() && it->second.txt.empty())
			it->second.txt = kv;
	}
}


}

void advertise(const std::string& service_type, const std::string& instance_label,
               std::uint16_t port, std::vector<std::pair<std::string, std::string>> txt) {
	auto* svc = new service_records();
	svc->service_type = service_type;
	svc->service_instance = sanitize_label(instance_label) + "." + service_type;
	svc->hostname_qualified = sanitize_label(hostname_label()) + ".local.";
	svc->port = port;
	svc->txt = std::move(txt);
	try {
		std::thread(responder_loop, svc).detach();
	} catch (...) {
		delete svc;
	}
}


std::vector<service_instance> discover(const std::string& service_type, int timeout_ms) {
	struct sockaddr_in saddr {};
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = 0;

	const int sock = mdns_socket_open_ipv4(&saddr);
	if (sock < 0)
		return {};

	std::vector<unsigned char> buffer(4096);
	const std::string type_lower = lower(service_type);
	const auto send_query = [&] {
		return mdns_query_send(sock, MDNS_RECORDTYPE_PTR, service_type.c_str(),
		                       service_type.size(), buffer.data(), buffer.size(), 0);
	};
	if (send_query() < 0) {
		mdns_socket_close(sock);
		return {};
	}

	std::map<std::string, service_instance> found;
	const long start = now_ms();
	const long deadline = start + static_cast<long>(timeout_ms);
	bool resent = false;

	for (;;) {
		const long now = now_ms();
		const long remaining = deadline - now;
		if (remaining <= 0)
			break;
		if (!resent && now - start > timeout_ms / 3) {
			static_cast<void>(send_query());
			resent = true;
		}

		fd_set readfs;
		FD_ZERO(&readfs);
		FD_SET(sock, &readfs);

		const long wait = resent ? remaining : std::min(remaining, timeout_ms / 3L);
		struct timeval timeout;
		timeout.tv_sec = wait / 1000L;
		timeout.tv_usec = (wait % 1000L) * 1000L;

		const int res = ::select(sock + 1, &readfs, nullptr, nullptr, &timeout);
		if (res < 0)
			break;
		if (res > 0 && FD_ISSET(sock, &readfs)) {
			packet_state pkt;
			pkt.service_type = type_lower;
			static_cast<void>(mdns_query_recv(sock, buffer.data(), buffer.size(), query_callback,
			                                  &pkt, 0));
			merge_packet(pkt, found);
		}
	}

	mdns_socket_close(sock);

	std::vector<service_instance> out;
	out.reserve(found.size());
	for (auto& [name, svc] : found)
		out.push_back(std::move(svc));
	return out;
}

std::optional<std::string> pick_ipv4(const service_instance& svc) {
	if (!svc.source_ip.empty() && std::ranges::find(svc.ipv4, svc.source_ip) != svc.ipv4.end())
		return svc.source_ip;
	const auto locals = local_ipv4();
	for (const auto& ip : svc.ipv4) {
		struct in_addr a {};
		if (::inet_pton(AF_INET, ip.c_str(), &a) != 1)
			continue;
		const bool on_subnet = std::ranges::any_of(locals, [&](const iface_v4& l) {
			return l.mask.s_addr != 0 &&
			       (a.s_addr & l.mask.s_addr) == (l.addr.s_addr & l.mask.s_addr);
		});
		if (on_subnet)
			return ip;
	}
	if (!svc.ipv4.empty())
		return svc.ipv4.front();
	if (!svc.source_ip.empty())
		return svc.source_ip;
	return std::nullopt;
}



}
