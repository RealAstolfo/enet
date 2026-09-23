#ifndef ENET_MDNS_SERVICE_HPP
#define ENET_MDNS_SERVICE_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// mDNS / DNS-SD service advertisement and discovery, over the mjansson/mdns
// single-header library (a Guix input).  Generalised from phobos: the
// service type and instance label are caller-supplied, so this carries no
// application-specific service names.

namespace enet::mdns {

// One discovered service instance and the addresses/metadata found for it.
struct service_instance {
  std::string instance;               // "<label>.<service_type>"
  std::string host;                   // SRV target hostname
  std::uint16_t port = 0;
  std::string source_ip;             // IP the response came from
  std::vector<std::string> ipv4;     // A records for `host`
  std::map<std::string, std::string> txt; // TXT key/values
};

using endpoint = std::pair<std::string, std::uint16_t>; // (ip, port)

// Advertise `service_type` (e.g. "_myapp._tcp.local.") with `instance_label`
// on `port` and the given TXT records.  Spawns a detached responder thread
// that answers queries until the process exits.
void advertise(const std::string &service_type, const std::string &instance_label,
               std::uint16_t port,
               std::vector<std::pair<std::string, std::string>> txt = {});

// Send a PTR query for `service_type` and collect the answers arriving within
// `timeout_ms`.
std::vector<service_instance> discover(const std::string &service_type, int timeout_ms = 4000);

// Choose the best IPv4 for `svc`: the responder's own source IP if it appears
// in the A records, else an A record on one of our subnets, else the first A
// record, else the source IP.
std::optional<std::string> pick_ipv4(const service_instance &svc);

} // namespace enet::mdns

#endif
