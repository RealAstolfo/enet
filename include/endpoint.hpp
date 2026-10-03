#ifndef ENDPOINT_HPP
#define ENDPOINT_HPP

#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2udpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

struct endpoint {
  sockaddr addr;
  socklen_t addrlen;
  std::string canonname;
  int family;
  int flags;
  int protocol;
  int socktype;

  endpoint(const addrinfo &info) {
    addr = *info.ai_addr;
    addrlen = info.ai_addrlen;
    if (info.ai_canonname != nullptr)
      canonname = std::string(info.ai_canonname);
    family = info.ai_family;
    flags = info.ai_flags;
    protocol = info.ai_protocol;
    socktype = info.ai_socktype;
  }

  endpoint(const std::string &b32) {
    canonname = b32;
  }

  // Dotted-quad IPv4 address + port: the loopback transports build {127.x.y.z, port} directly
  // without a resolver hop.
  endpoint(const char *ip, std::uint16_t port) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
      std::memset(&sa, 0, sizeof sa);
      sa.sin_family = AF_INET;
    }
    std::memcpy(&addr, &sa, sizeof sa);
    addrlen = sizeof sa;
    canonname = std::string(ip) + ":" + std::to_string(port);
    family = AF_INET;
    flags = 0;
    protocol = 0;
    socktype = SOCK_DGRAM;
  }

  endpoint() {}
};

#endif
