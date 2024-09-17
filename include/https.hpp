#ifndef HTTPS_HPP
#define HTTPS_HPP

#include <array>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "ssl.hpp"

struct https_resolver {
  ssl_resolver internal;
  std::vector<endpoint> resolve(const std::string &, const std::string &);
};

inline std::vector<endpoint>
https_resolver::resolve(const std::string &host, const std::string &service) {
  return internal.resolve(host, service);
}

struct https_socket {
  ssl_socket internal;
  std::string write_buffer;

  void connect(const std::vector<endpoint> &endpoints);
  std::string request(const std::string &data);
  void close();
};

void https_socket::connect(const std::vector<endpoint> &endpoints) {
  internal.connect(*(endpoints.begin()));
}

std::string https_socket::request(const std::string &data) {
  internal.send(data);
  std::string response;
  std::array<char, 4096> receive_buffer;

  std::size_t bytes = 0;
  do {
    bytes = internal.receive(receive_buffer);
    response.append(receive_buffer.data(), bytes);
  } while (bytes > 0);

  return response;
}

void https_socket::close() { internal.close(); }

#endif
