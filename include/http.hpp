#ifndef HTTP_HPP
#define HTTP_HPP

#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "endpoint.hpp"
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
  endpoint cached;
  // network buffer, fills when receiving, so i can abstract away the http later
  std::string buffer;

  bool bind(const endpoint &ep) { return internal.bind(ep); }
  bool listen(const int max_incoming_connections) {
    return internal.listen(max_incoming_connections);
  }

  http_socket accept() {
    http_socket hs;
    hs.internal = internal.accept();

    return hs;
  }

  bool connect(const endpoint &ep) {
    cached = ep;
    return internal.connect(ep);
  }

  std::string get(const std::string &uri) {
    if (internal.sockfd == -1)
      internal.connect(cached);

    std::string request_final;
    {
      std::stringstream request;
      request << "GET " << uri << " HTTP/1.1\r\n";
      request << "Host: " << cached.canonname << "\r\n";
      request << "Accept: */*\r\n";
      request << "Connection: close\r\n";
      request << "\r\n";

      // move data, prevents copying.
      request_final = std::move(request).str();
    }

    ssize_t sent_bytes = internal.send(request_final);
    if (sent_bytes < 0) {
      std::cerr << "Error sending data" << std::endl;
      return 0;
    }

    std::string response;
    std::array<char, 4096> receive_buffer;

    ssize_t bytes = 0;
    size_t header_end;
    do {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;
      response.append(std::data(receive_buffer), bytes);
      header_end = response.find("\r\n\r\n");
    } while (header_end == std::string::npos);

    bool should_close = false;
    std::string headers = response.substr(0, response.find("\r\n\r\n"));

    size_t connection_pos = headers.find("Connection:");
    if (connection_pos == std::string::npos) {
      size_t val_start = headers.find_first_not_of(" \t", connection_pos + 10);
      size_t val_end = headers.find("\r\n", val_start);
      if (val_end != std::string::npos)
        if (headers.substr(val_start, val_end - val_start) == "close")
          should_close = true;
    }

    do {
      bytes = internal.receive(receive_buffer);
      response.append(std::data(receive_buffer), bytes);
    } while (bytes > 0);

    if (should_close)
      internal.close();

    return response.substr(header_end + 4);
  }

  template <typename Container> ssize_t post(const Container &data) {
    if (internal.sockfd == -1)
      internal.connect(cached);

    std::string byte_stream_final;
    {
      std::stringstream byte_stream;
      // convert std::byte to a string representation.
      for (const auto &byte : data)
        byte_stream << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(byte);

      // move data, prevents copying.
      byte_stream_final = std::move(byte_stream).str();
    }

    std::string request_final;
    {
      std::stringstream request;
      request << "POST "
              << "/"
              << " HTTP/1.1\r\n";
      request << "Host: " << cached.canonname << "\r\n";
      request << "Content-Type: application/octet-stream\r\n";
      request << "Content-Length: " << byte_stream_final.length() << "\r\n";

      // always try to keep connection, will still close if server says to.
      request << "Connection: Keep-Alive\r\n";
      request << "\r\n";

      // move data, prevents copying.
      request << std::move(byte_stream_final);

      // move data, prevents copying.
      request_final = std::move(request).str();
    }

    ssize_t sent_bytes = internal.send(request_final);
    if (sent_bytes < (ssize_t)request_final.length())
      std::cerr << "Error: Incomplete send" << std::endl;

    return sent_bytes;
  }

  template <typename Container> ssize_t receive(Container &data) {
    std::array<char, 4096> receive_buffer;

    ssize_t bytes = 0;
    size_t header_end;
    do {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;

      buffer.append(std::data(receive_buffer), bytes);
      header_end = buffer.find("\r\n\r\n");
    } while (header_end == std::string::npos);

    bool should_close = false;
    std::string headers = buffer.substr(0, buffer.find("\r\n\r\n"));

    size_t connection_pos = headers.find("Connection:");
    if (connection_pos == std::string::npos) {
      size_t val_start = headers.find_first_not_of(" \t", connection_pos + 10);
      size_t val_end = headers.find("\r\n", val_start);
      if (val_end != std::string::npos)
        if (headers.substr(val_start, val_end - val_start) == "close")
          should_close = true;
    }

    size_t content_length = 0;
    size_t content_length_pos = headers.find("Content-Length:");
    if (content_length_pos != std::string::npos) {
      size_t val_start =
          headers.find_first_not_of(" \t", content_length_pos + 15);
      size_t val_end = headers.find("\r\n", val_start);
      if (val_end != std::string::npos)
        content_length =
            std::stoul(headers.substr(val_start, val_end - val_start));
    }

    while (buffer.length() - header_end - 4 < content_length) {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;
      buffer.append(std::data(receive_buffer), bytes);
    }

    // Assuming the content starts right after the "\r\n\r\n"
    std::string content = buffer.substr(header_end + 4);
    // Assuming the response content is represented in hex format
    for (size_t i = 0; i < content.length(); i += 2) {
      std::string byte_str = content.substr(i, 2);
      data[i / 2] = static_cast<typename Container::value_type>(
          std::stoi(byte_str, nullptr, 16));
    }

    // erase content from the buffer we just read, this will make any extra bits
    // we read left for the next read
    buffer.erase(0, header_end + content_length + 4);

    if (should_close)
      internal.close();

    return std::size(data);
  }

  template <typename T> ssize_t receive_into(T &obj) {
    union var {
      T obj;
      std::array<std::byte, sizeof(T)> bytes;
    };

    var v;
    ssize_t len = receive(v.bytes);
    obj = v.obj;
    return len;
  }

  void close() { internal.close(); }
};

#endif
