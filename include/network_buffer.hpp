#ifndef ENET_NETWORK_BUFFER_HPP
#define ENET_NETWORK_BUFFER_HPP

#include <cstddef>
#include <cstring>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>

/*
  Abstract away the lower level protocol by creating a buffer that makes
  everything look like a stream to higher level. Accepts a template typename for
  header info.

 */

template <typename Header> struct network_buffer {
  static_assert(std::is_member_object_pointer_v<decltype(&Header::msg_len)>,
                "Header does not have a member named msg_len");
  using buffer = std::vector<std::byte>;

  struct network_packet {
    Header header;
    buffer data;
  };

  template <typename Container> void add_data(Container &data) {
    internal.insert(std::end(internal),
                    std::make_move_iterator(std::begin(data)),
                    std::make_move_iterator(std::end(data)));
  }

  bool has_complete_packet() {
    if (std::size(internal) < sizeof(Header))
      return false;

    union {
      std::byte *addr;
      Header *hdr_addr;
    };

    addr = &internal[0];
    if (hdr_addr->msg_len >= std::size(internal) - sizeof(Header))
      return true;

    return false;
  }

  std::optional<network_packet> get_next() {
    if (!has_complete_packet())
      return std::nullopt;

    network_packet np;
    std::memcpy(&np.header, &internal[0], sizeof(Header));
    std::erase(np.data, std::begin(np.data) + sizeof(Header));
    np.data(std::make_move_iterator(std::begin(internal)),
            std::make_move_iterator(std::begin(internal) + np.header.msg_len));

    return np;
  }

  buffer internal;
};

#endif
