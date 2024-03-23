#ifndef HTTP_HPP
#define HTTP_HPP

#include <array>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include "endpoint.hpp"
#include "tcp.hpp"
#include "zstream.hpp"

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
  std::vector<std::byte> buffer;

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

  template <typename Container_In, typename Container_Out>
  Container_Out request(const Container_In &data) {
    if (internal.sockfd == -1)
      internal.connect(cached);

    std::string byte_stream_final;
    {
      std::ostringstream byte_stream;
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

    ssize_t bytes = internal.send(request_final);
    if (bytes < (ssize_t)request_final.length())
      std::cerr << "Error: Incomplete send" << std::endl;

    std::string_view view(reinterpret_cast<const char *>(std::data(buffer)),
                          std::size(buffer));
    std::array<std::byte, 4096> receive_buffer;

    size_t header_end = view.find("\r\n\r\n");
    while (header_end == std::string::npos) {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;
      std::copy(std::begin(receive_buffer), std::begin(receive_buffer) + bytes,
                std::back_inserter(buffer));

      view = std::string_view(reinterpret_cast<const char *>(std::data(buffer)),
                              std::size(buffer));
      header_end = view.find("\r\n\r\n");
    }

    std::string_view headers = view.substr(0, header_end);
    bool should_close = false;

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
        content_length = std::stoul(
            std::string(headers.substr(val_start, val_end - val_start)));
    }

    while (std::size(buffer) - header_end - 4 < content_length) {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;
      std::copy(std::begin(receive_buffer), std::begin(receive_buffer) + bytes,
                std::back_inserter(buffer));
    }

    view = std::string_view(reinterpret_cast<const char *>(std::data(buffer)),
                            std::size(buffer));

    std::string content;
    {
      std::string compressed_content = std::string(view);
      std::istringstream content_stream(std::move(compressed_content));
      zstream decompressor(&content_stream);
      decompressor >> content;
    }

    std::vector<std::byte> out(content.length() / 2);
    for (size_t i = 0; i < content.length(); i += 2) {
      out[i / 2] = static_cast<typename Container_Out::value_type>(
          std::stoi(std::string(content.substr(i, 2)), nullptr, 16));
    }

    buffer.erase(std::begin(buffer),
                 std::begin(buffer) + header_end + content_length + 4);

    if (should_close)
      internal.close();

    return out;
  }

  template <typename Container> void receive(Container &data) {
    ssize_t bytes = 0;
    std::string_view view(reinterpret_cast<const char *>(std::data(buffer)),
                          std::size(buffer));
    std::array<std::byte, 4096> receive_buffer;

    size_t header_end = view.find("\r\n\r\n");
    while (header_end == std::string::npos) {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;
      std::copy(std::begin(receive_buffer), std::begin(receive_buffer) + bytes,
                std::back_inserter(buffer));

      view = std::string_view(reinterpret_cast<const char *>(std::data(buffer)),
                              std::size(buffer));
      header_end = view.find("\r\n\r\n");
    }

    std::string_view headers = view.substr(0, header_end);
    bool should_close = false;

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
        content_length = std::stoul(
            std::string(headers.substr(val_start, val_end - val_start)));
    }

    while (std::size(buffer) - header_end - 4 < content_length) {
      bytes = internal.receive(receive_buffer);
      if (bytes < 0)
        break;
      std::copy(std::begin(receive_buffer), std::begin(receive_buffer) + bytes,
                std::back_inserter(buffer));
    }

    view = std::string_view(reinterpret_cast<const char *>(std::data(buffer)),
                            std::size(buffer));

    std::string_view content = view.substr(header_end + 4);

    data.resize(content.length() / 2);
    for (size_t i = 0; i < content.length(); i += 2) {
      data[i / 2] = static_cast<std::byte>(
          std::stoi(std::string(content.substr(i, 2)), nullptr, 16));
    }

    buffer.erase(std::begin(buffer),
                 std::begin(buffer) + header_end + content_length + 4);

    if (should_close)
      internal.close();
  }

  template <typename Container> void respond(const Container &data) {
    if (internal.sockfd == -1)
      internal.connect(cached);

    std::string byte_stream_final;
    {
      std::ostringstream byte_stream;
      // convert std::byte to a string representation.
      for (const auto &byte : data)
        byte_stream << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(byte);
      // move data, prevents copying.
      byte_stream_final = std::move(byte_stream).str();
    }

    std::string response_final;
    {
      std::stringstream response;
      response << "HTTP/1.1 200 OK\r\n";
      response << "Content-Type: application/octet-stream\r\n";
      response << "Content-Length: " << byte_stream_final.length() << "\r\n";

      // always try to keep connection, will still close if server says to.
      response << "Connection: Keep-Alive\r\n";
      response << "\r\n";

      // move data, prevents copying.
      response << std::move(byte_stream_final);

      // move data, prevents copying.
      response_final = std::move(response).str();
    }

    ssize_t bytes = internal.send(response_final);
    if (bytes < (ssize_t)response_final.length())
      std::cerr << "Error: Incomplete send" << std::endl;
  }

  template <typename T_in, typename T_out> T_out request_into(const T_in &obj) {
    union var_in {
      T_in obj;
      std::array<std::byte, sizeof(T_in)> bytes;
    };

    union var_out {
      T_out obj;
      std::array<std::byte, sizeof(T_out)> bytes;
    };

    var_in vin = obj;
    var_out vout;

    vout.bytes = request(vin.bytes);
    return vout;
  }

  template <typename T_in, typename T_out> T_out respond_into(const T_in &obj) {
    union var_in {
      T_in obj;
      std::array<std::byte, sizeof(T_in)> bytes;
    };

    union var_out {
      T_out obj;
      std::array<std::byte, sizeof(T_out)> bytes;
    };

    var_in vin = obj;
    var_out vout;

    vout.bytes = respond(vin.bytes);
    return vout;
  }

  void close() { internal.close(); }
};

#endif
