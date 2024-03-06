#ifndef ENDPOINT_HPP
#define ENDPOINT_HPP

#include <cstdint>
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

  endpoint() {}
};

#endif
