#include "network_buffer.hpp"
#include <cstdint>

int main() {

  struct header {
    uint32_t seq;
    uint32_t msg_len;
  };

  network_buffer<header> netbuf;

  return 0;
}
