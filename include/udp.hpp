#ifndef UDP_HPP
#define UDP_HPP

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

#include "endpoint.hpp"

struct udp_resolver {
  udp_resolver() {
#ifdef _WIN32
    WSADATA wsData;
    if (WSAStartup(MAKEWORD(2, 2), &wsData) != 0) {
      std::cerr << "Failed to initialize winsock." << std::endl;
    }
#endif
  }

  ~udp_resolver() {
#ifdef _WIN32
    WSACleanup();
#endif
  }

  std::vector<endpoint> resolve(const std::string &host,
                                const std::string &service) {
    struct addrinfo hints, *res;
    int status;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;    // IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP socket

    std::vector<endpoint> endpoints;
    if ((status = getaddrinfo(host.c_str(), service.c_str(), &hints, &res)) !=
        0) {
      std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
      return endpoints;
    }

    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
      endpoints.emplace_back(*p);
    }

    freeaddrinfo(res);
    return endpoints;
  }
};

struct udp_socket {
  udp_socket() : sockfd(-1) {}

  bool bind(const endpoint ep) {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
      std::cerr << "Failed to create socket." << std::endl;
      return false;
    }

    if (::bind(sockfd, reinterpret_cast<const sockaddr *>(&ep.addr),
               sizeof(ep.addr)) < 0) {
      std::cerr << "Bind failed." << std::endl;
      close();
      return false;
    }

    return true;
  }

  bool connect(const endpoint ep) {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
      std::cerr << "Failed to create socket." << std::endl;
      return false;
    }

    if (::connect(sockfd, reinterpret_cast<const sockaddr *>(&ep.addr),
                  sizeof(ep.addr)) < 0) {
      std::cerr << "Connection failed." << std::endl;
      close();
      return false;
    }

    return true;
  }

  template <typename Container>
  ssize_t send(const Container &data, const endpoint &to) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes_sent = ::sendto(sockfd, std::data(data), std::size(data),
                                  MSG_CONFIRM, &to.addr, to.addrlen);
    if (bytes_sent == -1) {
      std::cerr << "Failed to send data." << std::endl;
      return -1;
    }

    return bytes_sent;
  }

  template <typename Container>
  ssize_t receive(Container &buffer, endpoint &from) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes_read =
        ::recvfrom(sockfd, std::data(buffer), std::size(buffer), MSG_DONTWAIT,
                   &from.addr, &from.addrlen);
    if (bytes_read == -1) {
      std::cerr << "Failed to receive data." << std::endl;
      return -1;
    }

    return bytes_read;
  }

  void close() {
    if (sockfd != -1) {
#ifdef _WIN32
      closesocket(sockfd);
#else
      ::close(sockfd);
#endif
      sockfd = -1;
    }
  }

  int sockfd;
};

#endif
