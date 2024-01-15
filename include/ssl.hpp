#ifndef SSL_HPP
#define SSL_HPP

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2sslip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "endpoint.hpp"

struct ssl_resolver {
  ssl_resolver() {
#ifdef _WIN32
    WSADATA wsData;
    if (WSAStartup(MAKEWORD(2, 2), &wsData) != 0) {
      std::cerr << "Failed to initialize winsock." << std::endl;
    }
#endif
  }

  ~ssl_resolver() {
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
    hints.ai_family = AF_INET;       // AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP socket

    std::vector<endpoint> endpoints;
    if ((status = getaddrinfo(host.c_str(), service.c_str(), &hints, &res)) !=
        0) {
      std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
      return endpoints;
    }

    for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
      void *addr;
      bool is_ipv6;
      std::uint16_t port = 0;

      if (p->ai_family == AF_INET) {
        struct sockaddr_in *ipv4 =
            reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
        addr = &(ipv4->sin_addr);
        is_ipv6 = false;
        port = ntohs(ipv4->sin_port);
      } else {
        struct sockaddr_in6 *ipv6 =
            reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
        addr = &(ipv6->sin6_addr);
        is_ipv6 = true;
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

struct ssl_socket {
  int sockfd;
  SSL *ssl;
  SSL_CTX *ssl_ctx;

  ssl_socket() : sockfd(-1) {}

  bool bind(const endpoint ep) {
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

  bool listen(const int max_incoming_connections) {
    if (::listen(sockfd, max_incoming_connections) == -1) {
      std::cerr << "Listen Failed" << std::endl;
      close();
      return false;
    }

    return true;
  }

  bool connect(const endpoint ep) {
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
      std::cerr << "Failed to create socket." << std::endl;
      return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ep.host_port);

    if (inet_pton(AF_INET, ep.host_ip.c_str(), &(server_addr.sin_addr)) <= 0) {
      std::cerr << "Invalid address/Address not supported: "
                << ep.host_ip.c_str() << std::endl;
      close();
      return false;
    }

    if (::connect(sockfd, reinterpret_cast<sockaddr *>(&server_addr),
                  sizeof(server_addr)) < 0) {
      std::cerr << "Connection failed." << std::endl;
      close();
      return false;
    }

    // Initiate ssl part
    SSL_library_init();
    ssl_ctx = SSL_CTX_new(SSLv23_method());
    ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, sockfd);
    SSL_connect(ssl);

    return true;
  }

  ssize_t send(const std::string_view &data) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes_sent = SSL_write(ssl, data.data(), data.size());
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

    ssize_t bytes = SSL_read(ssl, buffer.data(), buffer.size() - 1);
    buffer[bytes] = '\0';
    if (bytes == -1) {
      std::cerr << "Failed to receive data." << std::endl;
      return -1;
    }

    return bytes;
  }

  void close() {

    // Shutdown SSL
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ssl_ctx);

    if (sockfd != -1) {
#ifdef _WIN32
      closesocket(sockfd);
#else
      ::close(sockfd);
#endif
      sockfd = -1;
    }
  }
};

inline ssl_socket &operator<<(ssl_socket &sock, const std::string &data) {
  sock.send(data);
  return sock;
}

inline ssl_socket &operator>>(ssl_socket &sock, std::string &data) {
  std::array<char, 4096> receive_buffer;

  std::size_t bytes = 0;
  do {
    bytes = sock.receive(receive_buffer);
    data.append(receive_buffer.data(), bytes);
  } while (bytes > 0);

  return sock;
}

#endif
