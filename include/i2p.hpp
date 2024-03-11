#ifndef I2P_HPP
#define I2P_HPP

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

#include "Destination.h"
#include "Identity.h"
#include "Streaming.h"
#include "api.h"
#include "endpoint.hpp"
#include "singleton.hpp"
#include "tcp.hpp"

const int I2P_STREAM_CONNECT_TIMEOUT = 30;

struct i2p_session {

  static i2p_session &instance() {
    static i2p_session session;
    return session;
  }

  static std::string
  generate_b32_address_from_destination(const std::string &destination);
  const i2p::data::PrivateKeys &generate_keys();
  const i2p::data::PrivateKeys &get_keys() const { return keys; };
  bool is_ready() const {
    return local_destination ? local_destination->IsReady() : false;
  }
  std::shared_ptr<i2p::client::ClientDestination>
  get_local_destination() const {
    return local_destination;
  };
  std::string get_b32_address() const;

  void start();
  void stop();

  std::shared_ptr<i2p::stream::Stream>
  connect(const i2p::data::IdentHash &ident, const uint16_t port);
  std::string resolve(const std::string &b32);

private:
  i2p_session();
  ~i2p_session();

  std::shared_ptr<const i2p::data::LeaseSet>
  request_lease_set(const i2p::data::IdentHash &ident);
  void handle_accept(std::shared_ptr<i2p::stream::Stream> stream);

public:
  i2p::data::PrivateKeys keys;
  std::shared_ptr<i2p::client::ClientDestination> local_destination;
  std::vector<std::shared_ptr<i2p::stream::Stream>> connection_streams;
};

inline void init_i2p(int argc, char *argv[], const char *app_name) {
  i2p::api::InitI2P(argc, argv, app_name);
  i2p::api::StartI2P();
}

inline void deinit_i2p() {
  i2p::api::StopI2P();
  i2p::api::TerminateI2P();
}

struct i2p_resolver {
  std::string resolve(const std::string &b32) {
    return i2p_session::instance().resolve(b32);
  }
};

struct i2p_socket {
  std::shared_ptr<i2p::stream::Stream> stream;

  bool connect(const std::string &b64, const uint16_t port) {
    i2p::data::IdentityEx ident;
    ident.FromBase64(b64);
    stream = i2p_session::instance().connect(ident.GetIdentHash(), port);
    if (!stream)
      return false;

    return true;
  }

  template <typename Container> ssize_t send(const Container &data) {
    ssize_t bytes_sent = stream->Send(
        reinterpret_cast<const uint8_t *>(std::data(data)), std::size(data));
    if (bytes_sent == -1) {
      std::cerr << "Failed to send data." << std::endl;
      return -1;
    }

    std::cerr << "Bytes Sent: " << bytes_sent << " Data Size: " << data.size()
              << std::endl;
    return bytes_sent;
  }

  template <typename Container> ssize_t receive(Container &buffer) {
    ssize_t bytes_read = 0;
    if (stream->GetStatus() == i2p::stream::eStreamStatusNew ||
        stream->GetStatus() == i2p::stream::eStreamStatusOpen) {
      bytes_read =
          stream->Receive(reinterpret_cast<uint8_t *>(std::data(buffer)),
                          std::size(buffer), std::numeric_limits<int>::max());
    } else {
      bytes_read = stream->ReadSome(
          reinterpret_cast<uint8_t *>(std::data(buffer)), std::size(buffer));
    }

    if (bytes_read == -1) {
      std::cerr << "Failed to receive data." << std::endl;
      return -1;
    }

    return bytes_read;
  }

  template <typename Container> ssize_t receive_some(Container &buffer) {
    size_t total_bytes_read = 0;
    while (total_bytes_read < std::size(buffer)) {
      ssize_t bytes_read = 0;
      const auto begin =
          std::addressof(*(std::begin(buffer) + total_bytes_read));
      const std::size_t left = std::size(buffer) - total_bytes_read;

      if (stream->GetStatus() == i2p::stream::eStreamStatusNew ||
          stream->GetStatus() == i2p::stream::eStreamStatusOpen) {
        bytes_read = stream->Receive(reinterpret_cast<uint8_t *>(begin), left,
                                     std::numeric_limits<int>::max());
      } else {
        bytes_read = stream->ReadSome(reinterpret_cast<uint8_t *>(begin), left);
      }

      if (bytes_read == -1) {
        std::cerr << "Failed to receive data." << std::endl;
        return -1;
      } else if (bytes_read > 0)
        total_bytes_read += bytes_read;
    }

    return total_bytes_read;
  }

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

  void close() { stream->Close(); }
};
#endif
