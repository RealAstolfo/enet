#ifndef ENET_DHT_HPP
#define ENET_DHT_HPP

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <locale>
#include <md5.h>
#include <netinet/in.h>
#include <random>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include "dht.h"
#include "endpoint.hpp"
#include "udp.hpp"

struct dht_service {
  std::array<std::uint8_t, 20> id;
  udp_socket internal;

  dht_service() {

    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<uint8_t> distribution(0, 255);

    for (auto &b : id)
      b = distribution(generator);

    udp_resolver resolver;
    const endpoint local = resolver.resolve("0.0.0.0", "6881").front();
    internal.bind(local);

    int rc;
    rc = fcntl(internal.sockfd, F_GETFL, 0);
    if (rc < 0) {
      std::cerr << "Could not get options" << std::endl;
      internal.close();
      return;
    }

    rc = fcntl(internal.sockfd, F_SETFL, (rc | O_NONBLOCK));
    if (rc < 0) {
      std::cerr << "Could not set options" << std::endl;
      internal.close();
      return;
    }

    rc = dht_init(internal.sockfd, 0,
                  reinterpret_cast<unsigned char *>(std::data(id)),
                  (unsigned char *)"JC\0\0");
    if (rc < 0) {
      std::cerr << "dht init error" << std::endl;
      internal.close();
      return;
    }

    std::vector<endpoint> bootstraps =
        resolver.resolve("dht.transmissionbt.com", "");
    {
      auto bittorrent = resolver.resolve("router.bittorrent.com", "");
      bootstraps.insert(std::end(bootstraps),
                        std::make_move_iterator(std::begin(bittorrent)),
                        std::make_move_iterator(std::end(bittorrent)));
    }

    std::uniform_int_distribution<decltype(RAND_MAX)> sleep(0, RAND_MAX);
    std::cout << "Bootstrap Count: " << std::size(bootstraps) << std::endl;
    for (const auto &ep : bootstraps) {
      char str[INET6_ADDRSTRLEN];
      if (ep.family == AF_INET)
        inet_ntop(AF_INET, &(((struct sockaddr_in *)&ep.addr)->sin_addr), str,
                  INET_ADDRSTRLEN);
      else
        inet_ntop(AF_INET6, &(((struct sockaddr_in *)&ep.addr)->sin_addr), str,
                  INET6_ADDRSTRLEN);
      std::cout << std::string(str) << std::endl;

      ((struct sockaddr_in *)&ep.addr)->sin_port = htons(6881); // default port.
      dht_ping_node(&ep.addr, ep.addrlen);
      std::this_thread::sleep_for(
          std::chrono::microseconds(sleep(generator) % 10000));
    }
  }

  ~dht_service() { dht_uninit(); }

  int periodic(std::time_t &time_to_sleep, dht_callback_t *callback,
               void *closure = nullptr) {
    std::array<std::uint8_t, 4096> buf;

    endpoint from;
    int rc = internal.receive(buf, from, 0);

    if (rc > 0) {
      buf[rc] = '\0';
      dht_periodic(std::data(buf), rc, (struct sockaddr *)&from.addr,
                   from.addrlen, &time_to_sleep, callback, closure);
    } else {
      dht_periodic(NULL, 0, NULL, 0, &time_to_sleep, callback, closure);
    }

    return rc;
  }

  template <typename Container>
  int search(Container &buf, std::uint16_t port, int af,
             dht_callback_t *callback, void *closure = nullptr) {
    return dht_search(std::data(buf), port, af, callback, closure);
  }

  int get_nodes(std::array<sockaddr_in, 500> &sin, int &num,
                std::array<sockaddr_in6, 500> &sin6, int &num6) {
    return dht_get_nodes(std::data(sin), &num, std::data(sin6), &num6);
  }

  int insert_node(std::array<unsigned char, 20> id, sockaddr *sa, int salen) {
    return dht_insert_node(std::data(id), sa, salen);
  }
};

/* Functions called by the DHT. */

int dht_sendto(int sockfd, const void *buf, int len, int flags,
               const struct sockaddr *to, int tolen) {

  char str[INET6_ADDRSTRLEN] = {0};
  if (to->sa_family == AF_INET)
    inet_ntop(AF_INET, &(((struct sockaddr_in *)to)->sin_addr), str,
              INET_ADDRSTRLEN);
  else
    inet_ntop(AF_INET6, &(((struct sockaddr_in *)to)->sin_addr), str,
              INET6_ADDRSTRLEN);

  std::cerr << "Sending over fd: " << sockfd << " Flags: " << flags
            << " Byte count: " << len << " To: " << std::string(str) << " On: "
            << (std::uint16_t)htons(((struct sockaddr_in *)to)->sin_port)
            << std::endl;
  return ::sendto(sockfd, buf, len, flags, to, tolen);
}

int dht_blacklisted(const struct sockaddr *sa, int salen) {
  (void)sa;
  (void)salen;
  return 0;
}

void dht_hash(void *hash_return, int hash_size, const void *v1, int len1,
              const void *v2, int len2, const void *v3, int len3) {
  static MD5_CTX ctx;
  MD5Init(&ctx);

  MD5Update(&ctx, (const std::uint8_t *)v1, len1);
  MD5Update(&ctx, (const std::uint8_t *)v2, len2);
  MD5Update(&ctx, (const std::uint8_t *)v3, len3);
  if (hash_size > 16)
    std::memset((char *)hash_return + 16, 0, hash_size - 16);
  MD5Final((std::uint8_t *)hash_return, &ctx);
}

int dht_random_bytes(void *buf, size_t size) {
  int fd, rc, save;

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return -1;

  rc = read(fd, buf, size);

  save = errno;
  close(fd);
  errno = save;

  return rc;
}

#endif
