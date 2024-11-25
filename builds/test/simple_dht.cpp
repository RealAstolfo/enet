#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>

#include "dht.hpp"
#include "endpoint.hpp"

/* The call-back function is called by the DHT whenever something
   interesting happens.  Right now, it only happens when we get a new value or
   when a search completes, but this may be extended in future versions. */
static void callback(void *closure, int event, const unsigned char *info_hash,
                     const void *data, size_t data_len) {
  (void)closure;
  (void)info_hash;

  if (event == DHT_EVENT_SEARCH_DONE) {
    printf("Search done.\n");

  } else if (event == DHT_EVENT_SEARCH_DONE6) {
    printf("IPv6 search done.\n");
  } else if (event == DHT_EVENT_VALUES) {
    printf("Received %d values.\n", (int)(data_len / 6));
    char str[INET_ADDRSTRLEN];
    for (std::size_t i = 0; i < data_len; i += 6) {
      inet_ntop(AF_INET, ((const char *)data) + i, str, INET_ADDRSTRLEN);
      std::cout << "Value from info_hash: " << std::string(str) << ":"
                << ntohs(((const short *)((const char *)(data) + i))[2])
                << std::endl;
    }

  } else if (event == DHT_EVENT_VALUES6) {
    printf("Received %d IPv6 values.\n", (int)(data_len / 18));
    char str[INET6_ADDRSTRLEN];
  } else
    printf("Unknown DHT event %d.\n", event);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // dht_debug = stderr;
  // Use dht info_hash in an argument to the program
  std::array<unsigned char, 20> info_hash;

  dht_service dht;

  std::time_t tosleep = 0;

  std::size_t print_countdown = 20;
  std::size_t loop = 0;

  std::cerr << "Looking for: ";
  for (std::size_t i = 0; i < 20; i++) {
    std::string byte_string;
    byte_string += argv[1][i * 2];
    byte_string += argv[1][i * 2 + 1];
    std::cerr << byte_string;
    char *end;
    unsigned char byte = std::strtol(byte_string.c_str(), &end, 16);
    info_hash[i] = byte;
  }
  std::cerr << std::endl;

  std::thread searcher([&]() {
    while (true) {
      dht.search(info_hash, 12345, AF_INET, callback);
      // dht.find_node(info_hash, AF_INET);

      char str[INET6_ADDRSTRLEN];

      auto s = dht.get_node(info_hash, AF_INET);
      std::cout << "Family: " << s.ss_family << std::endl;
      if (s.ss_family == AF_INET)
        inet_ntop(AF_INET, &(((struct sockaddr_in *)&s)->sin_addr), str,
                  INET_ADDRSTRLEN);
      else
        inet_ntop(AF_INET6, &(((struct sockaddr_in *)&s)->sin_addr), str,
                  INET6_ADDRSTRLEN);
      std::cout << "Found Peer: " << std::string(str) << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  });
  searcher.detach();

  while (++loop) {
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

    // TODO: query IP info for node using id

    /* This is how you trigger a search for a torrent hash.  If port
   (the second argument) is non-zero, it also performs an announce.
   Since peers expire announced data after 30 minutes, it is a good
   idea to reannounce every 28 minutes or so. */
    if (loop % print_countdown) {
      loop = 0;
      dht.search(info_hash, 12345, AF_INET, callback);

      // {
      //   std::array<sockaddr_in, 500> sin;
      //   std::array<sockaddr_in6, 500> sin6;
      //   int num = std::size(sin), num6 = std::size(sin6);
      //   int i;
      //   i = dht.get_nodes(sin, num, sin6, num6);
      //   printf("Found %d (%d + %d) good nodes.\n", i, num, num6);
      // }

      // dht_dump_tables(stdout);
    }

    usleep(tosleep);
  }

  return 0;
}
