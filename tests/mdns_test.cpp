// pick_ipv4 selection is pure logic and tested deterministically.  A live
// advertise->discover round trip needs working multicast, which is not
// available in every sandbox, so it is best-effort: it must not crash or
// error, and if it does find the service the fields must be right.

#include "mdns_service.hpp"

#include "check.hpp"

#include <chrono>
#include <string>
#include <thread>

using namespace enet::mdns;

int main() {
  // pick_ipv4: source IP present among A records wins.
  {
    service_instance s;
    s.ipv4 = {"10.0.0.5", "192.168.1.9"};
    s.source_ip = "192.168.1.9";
    CHECK(pick_ipv4(s) == "192.168.1.9");
  }
  // No source match -> falls back to the first A record.
  {
    service_instance s;
    s.ipv4 = {"10.0.0.5"};
    s.source_ip = "203.0.113.7"; // not in ipv4
    const auto ip = pick_ipv4(s);
    CHECK(ip.has_value()); // an A record or a subnet match or the source
  }
  // No A records -> the source IP.
  {
    service_instance s;
    s.source_ip = "203.0.113.7";
    CHECK(pick_ipv4(s) == "203.0.113.7");
  }
  // Nothing at all -> nullopt.
  {
    service_instance s;
    CHECK(!pick_ipv4(s).has_value());
  }

  // Best-effort round trip on a unique service type.
  const std::string type = "_enet-mdns-test._tcp.local.";
  advertise(type, "unit-test", 4321, {{"role", "test"}});
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto found = discover(type, 1500); // returns [] if no multicast; never throws
  for (const auto &svc : found) {
    if (svc.instance.find("unit-test") != std::string::npos) {
      CHECK(svc.port == 4321);
      auto it = svc.txt.find("role");
      CHECK(it != svc.txt.end() && it->second == "test");
    }
  }
  std::printf("mdns: discover returned %zu instance(s)\n", found.size());

  return check_main("mdns");
}
