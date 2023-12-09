#ifndef SOCKS4_HPP
#define SOCKS4_HPP

#include <cstddef>
#include <cstdint>
#include <string>

enum socks4_cmd : std::uint8_t {
  establish_tcp_stream = 0x01,
  establish_tcp_bind = 0x02,
};

struct socks4_request {
  std::uint8_t socks4_version = 0x04;
  socks4_cmd cmd;
  std::uint16_t dstport;
  std::uint64_t destip; // must be in network order
  std::string id;
};

enum socks4a_reply_code : std::uint8_t {
  req_granted = 0x5A,
  req_rej_or_fail = 0x5B,
  req_fail_client_not_running_identd = 0x5C,
  req_fail_client_identd_not_confirm = 0x5D,
};

struct socks4_response {
  std::uint8_t vn;
  socks4a_reply_code rep;
};

#endif
