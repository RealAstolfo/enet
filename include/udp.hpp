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

#include "common/networking/endpoint.hpp"

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
    char ip_address[INET6_ADDRSTRLEN];

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
      void *addr;
      std::uint16_t port = 0;

      bool is_ipv6 = (p->ai_family != AF_INET);
      if (is_ipv6) {
        struct sockaddr_in *ipv4 =
            reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
        addr = &(ipv4->sin_addr);
        port = ntohs(ipv4->sin_port);
      } else {
        struct sockaddr_in6 *ipv6 =
            reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
        addr = &(ipv6->sin6_addr);
        port = ntohs(ipv6->sin6_port);
      }

      // Convert the IP address to a string
      inet_ntop(p->ai_family, addr, ip_address, sizeof ip_address);
      endpoints.emplace_back(ip_address, port, is_ipv6);
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

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ep.host_port);

    if (inet_pton(AF_INET, ep.host_ip.c_str(), &(server_addr.sin_addr)) <= 0) {
      std::cerr << "Invalid address/Address not supported." << std::endl;
      close();
      return false;
    }

    if (::bind(sockfd, reinterpret_cast<sockaddr *>(&server_addr),
               sizeof(server_addr)) < 0) {
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

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ep.host_port);

    if (inet_pton(AF_INET, ep.host_ip.c_str(), &(server_addr.sin_addr)) <= 0) {
      std::cerr << "Invalid address/Address not supported." << std::endl;
      close();
      return false;
    }

    if (::connect(sockfd, reinterpret_cast<sockaddr *>(&server_addr),
                  sizeof(server_addr)) < 0) {
      std::cerr << "Connection failed." << std::endl;
      close();
      return false;
    }

    return true;
  }

  ssize_t send(const std::string_view &data) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes_sent = ::send(sockfd, data.data(), data.size(), 0);
    if (bytes_sent == -1) {
      std::cerr << "Failed to send data." << std::endl;
      return -1;
    }

    return bytes_sent;
  }

  ssize_t receive(std::array<char, 4096> &buffer) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes_read = ::recv(sockfd, buffer.data(), buffer.size(), 0);
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

udp_socket &operator<<(udp_socket &sock, const std::string &data) {
  sock.send(data);
  return sock;
}

udp_socket &operator>>(udp_socket &sock, std::string &data) {
  std::array<char, 4096> receive_buffer;

  std::size_t bytes = 0;
  do {
    bytes = sock.receive(receive_buffer);
    data.append(receive_buffer.data(), bytes);
  } while (bytes > 0);

  return sock;
}

#endif
