#ifndef ENET_TCP_LISTENER_HPP
#define ENET_TCP_LISTENER_HPP

// enet/tcp_listener.hpp --- header-only helpers that create a bound+listening
// TCP server socket, or a connected TCP client socket, and hand back the raw
// file descriptor.  The fd can be used directly with socket_io.hpp's write_all
// / read_full, or adopted into a tcp_socket (see tcp.hpp's tcp_socket(int)).
//
// Both helpers resolve their address with getaddrinfo, so IPv4 and IPv6 are
// supported: leave `family` at AF_UNSPEC to accept whichever the address (or
// the host's resolver) yields, or pin it to AF_INET / AF_INET6.  Every returned
// address is tried in turn, so a dual-stack host transparently falls back from
// one family to the other.

#include <cstdint>
#include <string>

#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace enet {

struct tcp_listener_opts {
  std::string bind_addr;      // "" / "0.0.0.0" / "::" for wildcard, or a literal IP.
  uint16_t port = 0;          // 0 => kernel-assigned ephemeral port.
  int backlog = 4;            // listen() backlog.
  bool reuseaddr = true;      // SO_REUSEADDR (fast rebind after restart).
  bool nonblocking = false;   // put the listening socket in O_NONBLOCK mode.
  int family = AF_UNSPEC;     // AF_INET, AF_INET6, or AF_UNSPEC (either).
};

// Create a TCP socket bound to `o.bind_addr:o.port` and put it in the listening
// state.  Returns the listening fd, or -1 on failure (with errno preserved from
// the last failing bind/listen).
//
// For an IPv6 wildcard bind, IPV6_V6ONLY is cleared so the same socket also
// accepts IPv4 clients (as v4-mapped addresses) where the kernel allows it.
[[nodiscard]] inline int make_tcp_listener(const tcp_listener_opts &o) {
  ::addrinfo hints{};
  hints.ai_family = o.family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // wildcard when node == nullptr.

  const char *node = o.bind_addr.empty() ? nullptr : o.bind_addr.c_str();
  std::string const port_s = std::to_string(o.port);

  ::addrinfo *res = nullptr;
  if (::getaddrinfo(node, port_s.c_str(), &hints, &res) != 0) {
    return -1;
  }

  int fd = -1;
  for (::addrinfo *p = res; p != nullptr; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
    if (fd < 0) {
      continue;
    }

    if (o.reuseaddr) {
      int one = 1;
      ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    if (p->ai_family == AF_INET6) {
      int off = 0;
      ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    }

    if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0 &&
        ::listen(fd, o.backlog) == 0) {
      break; // success.
    }

    int const err = errno;
    ::close(fd);
    fd = -1;
    errno = err;
  }
  ::freeaddrinfo(res);

  if (fd < 0) {
    return -1;
  }

  if (o.nonblocking) {
    int const flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  }
  return fd;
}

struct tcp_connect_opts {
  std::string host;
  uint16_t port = 0;
  bool tcp_nodelay = false; // TCP_NODELAY: disable Nagle for latency.
  bool keepalive = false;   // SO_KEEPALIVE: detect dead peers.
  int family = AF_UNSPEC;   // AF_INET, AF_INET6, or AF_UNSPEC (either).
};

// Resolve `o.host:o.port` and connect a TCP socket to the first address that
// accepts.  Returns the connected fd, or -1 on failure (errno preserved from
// the last failing connect).
[[nodiscard]] inline int make_tcp_client(const tcp_connect_opts &o) {
  ::addrinfo hints{};
  hints.ai_family = o.family;
  hints.ai_socktype = SOCK_STREAM;

  ::addrinfo *res = nullptr;
  std::string const port_s = std::to_string(o.port);
  if (::getaddrinfo(o.host.c_str(), port_s.c_str(), &hints, &res) != 0) {
    return -1;
  }

  int fd = -1;
  for (::addrinfo *p = res; p != nullptr; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      break; // success.
    }
    int const err = errno;
    ::close(fd);
    fd = -1;
    errno = err;
  }
  ::freeaddrinfo(res);

  if (fd < 0) {
    return -1;
  }

  if (o.tcp_nodelay) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  }
  if (o.keepalive) {
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
  }
  return fd;
}

} // namespace enet

#endif // ENET_TCP_LISTENER_HPP
