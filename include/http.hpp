#ifndef HTTP_HPP
#define HTTP_HPP

#include <array>
#include <iostream>
#include <streambuf>
#include <string>

#include "tcp.hpp"

struct http_resolver {
  tcp_resolver internal;
  std::vector<endpoint> resolve(const std::string &, const std::string &);
};

inline std::vector<endpoint>
http_resolver::resolve(const std::string &host, const std::string &service) {
  return internal.resolve(host, service);
}

struct http_socket {
  tcp_socket internal;
  std::string write_buffer;

  void connect(const std::vector<endpoint> &endpoints);
  std::string request(const std::string &data);
  void close();
};

void http_socket::connect(const std::vector<endpoint> &endpoints) {
  internal.connect(*endpoints.begin());
}

std::string http_socket::request(const std::string &data) {
  write_buffer = data;
  internal.send(write_buffer);
  std::string response;
  std::array<char, 4096> receive_buffer;

  std::size_t bytes = 0;
  do {
    bytes = internal.receive(receive_buffer);
    response.append(receive_buffer.data(), bytes);
  } while (bytes > 0);

  return response;
}

void http_socket::close() { internal.close(); }

#endif
