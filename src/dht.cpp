#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <sys/socket.h>

#include "udp.hpp"

struct node {
  std::array<std::uint8_t, 20> id;
  sockaddr_storage ss;
  std::size_t sslen;
  std::chrono::time_point<std::chrono::high_resolution_clock>
      time; /* time of last message received */
  std::chrono::time_point<std::chrono::high_resolution_clock>
      reply_time; /* time of last correct reply received */
  std::chrono::time_point<std::chrono::high_resolution_clock>
      pinged_time;    /* time of last request */
  std::size_t pinged; /* how many requests we sent since last reply */
  node *next;
};

struct bucket {
  int af;
  std::array<std::uint8_t, 20> first;
  std::size_t count;     /* number of nodes */
  std::size_t max_count; /* max number of nodes for this bucket */
  std::chrono::time_point<std::chrono::high_resolution_clock>
      time; /* time of last reply in this bucket */
  node *nodes;
  sockaddr_storage cached; /* the address of a likely candidate */
  std::size_t cached_len;
  bucket *next;
};

struct search_node {
  std::array<std::uint8_t, 20> id;
  sockaddr_storage ss;
  std::size_t sslen;
  std::chrono::time_point<std::chrono::high_resolution_clock>
      request_time; /* time of the last unanswered request */
  std::chrono::time_point<std::chrono::high_resolution_clock>
      reply_time; /* time of the last reply */
  std::size_t pinged;
  std::array<std::uint8_t, 40> token;
  std::size_t token_len;
  bool replied; /* whether we have received a reply */
  bool acked;   /* whether they acked our announcement */
};

constexpr std::size_t search_nodes = 14;

struct search {
  std::uint16_t tid;
  int af;
  std::chrono::time_point<std::chrono::high_resolution_clock>
      step_time; /* time of the last search_step */
  std::array<std::uint8_t, 20> id;
  std::uint16_t port; /* 0 for pure searches */
  bool done;
  std::array<search_node, search_nodes> nodes;
  std::size_t num_nodes;
  search *next;
};

struct peer {
  std::chrono::time_point<std::chrono::high_resolution_clock> time;
  std::array<std::uint8_t, 16> ip;
  std::uint16_t len;
  std::uint16_t port;
};

constexpr std::size_t max_peers = 2048;
constexpr std::size_t max_hashes = 16384;
constexpr std::size_t max_searches = 1024;
constexpr std::size_t search_time_expire = (62 * 60);
constexpr std::size_t max_inflight_requests = 4;
constexpr std::size_t search_retransmit = 10;

struct storage {
  std::array<std::uint8_t, 20> id;
  std::size_t num_peers, max_peers;
  peer *peers;
  storage *next;
};

enum message_type {
  ERROR = 0,
  REPLY = 1,
  PING = 2,
  FIND_NODE = 3,
  GET_PEERS = 4,
  ANNOUNCE_PEER = 5,
};

enum want_type {
  IPv4 = 1,
  IPv6 = 2,
};

constexpr std::size_t TID_LEN = 16;
constexpr std::size_t TOKEN_LEN = 128;
constexpr std::size_t NODES_LEN = (26 * 16);
constexpr std::size_t NODES6_LEN = (38 * 16);
constexpr std::size_t VALUES_LEN = 2048;
constexpr std::size_t VALUES6_LEN = 2048;

struct parsed_message {
  std::array<std::uint8_t, TID_LEN> tid;
  std::uint16_t tid_len;
  std::array<std::uint8_t, 20> id;
  std::array<std::uint8_t, 20> info_hash;
  std::array<std::uint8_t, 20> target;
  std::uint16_t port;
  std::uint16_t implied_port;
  std::array<std::uint8_t, TOKEN_LEN> token;
  std::uint16_t token_len;
  std::array<std::uint8_t, NODES_LEN> nodes;
  std::uint16_t nodes_len;
  std::array<std::uint8_t, NODES6_LEN> nodes6;
  std::uint16_t nodes6_len;
  std::array<std::uint8_t, VALUES_LEN> values;
  std::uint16_t values_len;
  std::array<std::uint8_t, VALUES6_LEN> values6;
  std::uint16_t values6_len;
  std::uint16_t want;

  parsed_message(const std::string &buf) {
    if (buf.back() != '\0') {
      return;
    }

    const std::string tid_match = "1:t";
    auto it = std::search(std::begin(buf), std::end(buf), std::begin(tid_match),
                          std::end(tid_match));

    if (it != std::end(buf)) {
      char *q;
      tid_len = std::strtol(&(*it) + 3, &q, 10);
      if (q && *q == ':' && tid_len > 0 && tid_len < TID_LEN)
        for (std::size_t i = 0; i < tid_len; i++)
          tid[i] = q[i];
    }

    const std::string id_match = "2:id20:";
    it = std::search(std::begin(buf), std::end(buf), std::begin(id_match),
                     std::end(id_match));

    if (it != std::end(buf)) {
      for (std::size_t i = 0; i < std::size(id); i++)
        id[i] = *(it + i); // check this.
    }

    const std::string info_hash_match = "9:info_hash20:";
    it = std::search(std::begin(buf), std::end(buf),
                     std::begin(info_hash_match), std::end(info_hash_match));

    if (it != std::end(buf)) {
      for (std::size_t i = 0; i < std::size(info_hash); i++)
        info_hash[i] = *(it + i); // check this.
    }

    const std::string port_match = "4:porti";
    it = std::search(std::begin(buf), std::end(buf), std::begin(port_match),
                     std::end(port_match));

    if (it != std::end(buf)) {
      char *q;
      long l = std::strtol(&(*it) + 7, &q, 10);
      if (q && *q == 'e' && l > 0 && l < 0x10000)
        port = l;
    }

    const std::string implied_port_match = "4:porti";
    it = std::search(std::begin(buf), std::end(buf),
                     std::begin(implied_port_match),
                     std::end(implied_port_match));

    if (it != std::end(buf)) {
      char *q;
      long l = std::strtol(&(*it) + 16, &q, 10);
      if (q && *q == 'e' && l > 0 && l < 0x10000)
        implied_port = l;
    }

    const std::string target_match = "6:target20:";
    it = std::search(std::begin(buf), std::end(buf), std::begin(target_match),
                     std::end(target_match));

    if (it != std::end(buf)) {
      for (std::size_t i = 0; i < std::size(target); i++)
        target[i] = *(it + i); // check this.
    }

    const std::string token_match = "5:token";
    it = std::search(std::begin(buf), std::end(buf), std::begin(token_match),
                     std::end(token_match));

    if (it != std::end(buf)) {
      char *q;
      token_len = std::strtol(&(*it) + 7, &q, 10);
      if (q && *q == ':' && token_len > 0 && token_len < TOKEN_LEN)
        for (std::size_t i = 0; i < token_len; i++)
          token[i] = q[i + 1];
    }

    const std::string nodes_match = "5:nodes";
    it = std::search(std::begin(buf), std::end(buf), std::begin(nodes_match),
                     std::end(nodes_match));

    if (it != std::end(buf)) {
      char *q;
      nodes_len = std::strtol(&(*it) + 7, &q, 10);
      if (q && *q == ':' && nodes_len > 0 && nodes_len <= NODES_LEN)
        for (std::size_t i = 0; i < nodes_len; i++)
          nodes[i] = q[i + 1];
    }

    const std::string nodes6_match = "6:nodes6";
    it = std::search(std::begin(buf), std::end(buf), std::begin(nodes6_match),
                     std::end(nodes6_match));

    if (it != std::end(buf)) {
      char *q;
      nodes6_len = std::strtol(&(*it) + 8, &q, 10);
      if (q && *q == ':' && nodes6_len > 0 && nodes6_len <= NODES6_LEN)
        for (std::size_t i = 0; i < nodes6_len; i++)
          nodes6[i] = q[i + 1];
    }

    const std::string values_match = "6:valuesl";
    it = std::search(std::begin(buf), std::end(buf), std::begin(values_match),
                     std::end(values_match));

    if (it != std::end(buf)) {
      // TODO: https://github.com/jech/dht/blob/master/dht.c#L2996
    }
  }
};
