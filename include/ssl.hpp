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

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP socket

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

    if (::bind(sockfd, reinterpret_cast<const sockaddr *>(&ep.addr),
               sizeof(ep.addr)) < 0) {
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

  ssl_socket accept() {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssl_socket client_socket;
    client_socket.sockfd =
        ::accept(sockfd, (sockaddr *)&client_addr, &client_len);
    if (client_socket.sockfd == -1) {
      std::cerr << "Accept failed" << std::endl;
    }

    return client_socket;
  }

  bool connect(const endpoint ep) {
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

    // Initiate ssl part
    SSL_library_init();
    ssl_ctx = SSL_CTX_new(SSLv23_method());
    ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, sockfd);
    SSL_connect(ssl);

    return true;
  }

  template <typename Container> ssize_t send(const Container &data) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes_sent = SSL_write(ssl, std::data(data), std::size(data));
    if (bytes_sent == -1) {
      std::cerr << "Failed to send data." << std::endl;
      return -1;
    }

    return bytes_sent;
  }

  template <typename Container> ssize_t receive(Container &buffer) {
    if (sockfd == -1) {
      std::cerr << "Socket not connected." << std::endl;
      return -1;
    }

    ssize_t bytes = SSL_read(ssl, std::data(buffer), std::size(buffer) - 1);
    buffer[bytes] = '\0';
    if (bytes == -1) {
      std::cerr << "Failed to receive data." << std::endl;
      return -1;
    }

    return bytes;
  }

  template <typename Container> ssize_t receive_some(Container &buffer) {
    size_t total_bytes_read = 0;
    while (total_bytes_read < std::size(buffer)) {
      if (sockfd == -1) {
        std::cerr << "Socket not connected." << std::endl;
        return -1;
      }

      const auto begin =
          std::addressof(*(std::begin(buffer) + total_bytes_read));
      const std::size_t left = std::size(buffer) - total_bytes_read;
      ssize_t bytes_read = ::recv(sockfd, begin, left, 0);
      if (bytes_read == -1) {
        std::cerr << "Failed to receive data." << std::endl;
        return -1;
      } else if (bytes_read > 0)
        total_bytes_read += bytes_read;
    }

    return total_bytes_read;
  }

  /*
    Receive exactly this object.
   */
  template <typename T> ssize_t receive_into(T &obj) {
    union var {
      T obj;
      std::array<std::byte, sizeof(T)> bytes;
    };

    var v;
    ssize_t len = receive_some(v.bytes);
    obj = v.obj;
    return len;
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
