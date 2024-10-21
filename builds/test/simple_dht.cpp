#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>

#include "dht.hpp"
#include "endpoint.hpp"

/* The call-back function is called by the DHT whenever something
   interesting happens.  Right now, it only happens when we get a new value or
   when a search completes, but this may be extended in future versions. */

static void callback(void *closure, int event, const unsigned char *info_hash,
                     const void *data, size_t data_len) {
  (void)closure;
  (void)event;
  (void)info_hash;
  (void)data;
  if (event == DHT_EVENT_SEARCH_DONE)
    printf("Search done.\n");
  else if (event == DHT_EVENT_SEARCH_DONE6)
    printf("IPv6 search done.\n");
  else if (event == DHT_EVENT_VALUES)
    printf("Received %d values.\n", (int)(data_len / 6));
  else if (event == DHT_EVENT_VALUES6)
    printf("Received %d IPv6 values.\n", (int)(data_len / 18));
  else
    printf("Unknown DHT event %d.\n", event);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  dht_service dht;

  std::time_t tosleep = 0;

  bool searching = true;
  std::size_t countdown = 20;
  while (countdown--) {
    int rc = dht.periodic(tosleep, callback, NULL);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        if (rc == EINVAL || rc == EFAULT)
          break;
        tosleep = 1;
      }
    }

    const unsigned char hash[20] = {0x54, 0x5a, 0x87, 0x89, 0xdf, 0xc4, 0x23,
                                    0xef, 0xf6, 0x03, 0x1f, 0x81, 0x94, 0xa9,
                                    0x3a, 0x16, 0x98, 0x8b, 0x72, 0x7b};
    /* This is how you trigger a search for a torrent hash.  If port
   (the second argument) is non-zero, it also performs an announce.
   Since peers expire announced data after 30 minutes, it is a good
   idea to reannounce every 28 minutes or so. */
    if (searching) {
      dht.search(hash, 0, AF_INET6, callback);
      dht.search(hash, 0, AF_INET, callback);
      searching = false;
    }

    sleep(tosleep);
  }

  {
    std::array<sockaddr_in, 500> sin;
    std::array<sockaddr_in6, 500> sin6;
    int num = std::size(sin), num6 = std::size(sin6);
    int i;
    i = dht.get_nodes(sin, num, sin6, num6);
    printf("Found %d (%d + %d) good nodes.\n", i, num, num6);
  }

  dht_dump_tables(stdout);
  return 0;
}
