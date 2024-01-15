#ifndef ENDPOINT_HPP
#define ENDPOINT_HPP

#include <cstdint>
#include <string>

struct endpoint {
  std::string host_ip;
  std::uint16_t host_port;
  bool ipv6;

  endpoint(std::string host_ip, std::uint16_t host_port, bool ipv6)
      : host_ip(host_ip), host_port(host_port), ipv6(ipv6) {}
};

#endif
