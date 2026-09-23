// enet socket_io / tcp_listener unit tests.
//
// Covers, over real kernel sockets:
//   * write_all + read_full happy path across a socketpair, with a non-blocking
//     writer and small buffers so the partial-write / poll(POLLOUT) path is hit;
//   * write_all returning false when the peer has closed (EPIPE);
//   * read_full returning a short count on a clean peer-close (EOF);
//   * make_udp_socket + udp_recv over IPv4 loopback;
//   * a make_tcp_listener / make_tcp_client round trip with an echo, wrapping
//     the accepted and connected fds in tcp_socket via its adopt constructor;
//   * the same TCP round trip over IPv6 loopback when the host provides it
//     (skipped, not failed, when ::1 is unavailable).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket_io.hpp"
#include "tcp.hpp"
#include "tcp_listener.hpp"

#include "check.hpp"

namespace {

// Recover the local port a socket is bound to (works for IPv4 and IPv6).
uint16_t port_of(int fd) {
  sockaddr_storage ss{};
  socklen_t sl = sizeof(ss);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&ss), &sl) != 0) {
    return 0;
  }
  if (ss.ss_family == AF_INET6) {
    return ntohs(reinterpret_cast<sockaddr_in6 *>(&ss)->sin6_port);
  }
  return ntohs(reinterpret_cast<sockaddr_in *>(&ss)->sin_port);
}

void set_buf(int fd, int bytes) {
  ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));
  ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
}

void set_nonblocking(int fd) {
  int const fl = ::fcntl(fd, F_GETFL, 0);
  if (fl >= 0) {
    ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  }
}

// -------------------------------------------------------------------------
// write_all + read_full happy path (large transfer, forced segmentation).
// -------------------------------------------------------------------------
void test_write_read_roundtrip() {
  int sp[2];
  CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
  set_buf(sp[0], 4096);
  set_buf(sp[1], 4096);
  set_nonblocking(sp[0]); // exercise write_all's EAGAIN + poll(POLLOUT) branch.

  constexpr size_t kN = 1u << 20; // 1 MiB.
  std::vector<std::byte> payload(kN);
  for (size_t i = 0; i < kN; ++i) {
    payload[i] = static_cast<std::byte>(i * 31u + 7u);
  }

  std::vector<std::byte> got(kN);
  ssize_t received = -1;
  std::thread reader([&] {
    // span overload of read_full; the socket is blocking on this end.
    received = enet::read_full(sp[1], std::span<std::byte>(got));
  });

  // ByteRange (span) overload of write_all.
  bool const ok = enet::write_all(sp[0], std::span<const std::byte>(payload));
  reader.join();

  CHECK(ok);
  CHECK(received == static_cast<ssize_t>(kN));
  CHECK(std::memcmp(payload.data(), got.data(), kN) == 0);

  ::close(sp[0]);
  ::close(sp[1]);
}

// -------------------------------------------------------------------------
// write_all reports failure once the peer has closed (EPIPE).
// -------------------------------------------------------------------------
void test_write_all_peer_close() {
  int sp[2];
  CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
  ::close(sp[1]); // peer gone.

  std::vector<std::byte> payload(4u << 20, std::byte{0xAB}); // 4 MiB, won't fit.
  // Short stall deadline so a hypothetical wedge still returns promptly; in
  // practice send() returns EPIPE and write_all fails immediately.
  bool const ok =
      enet::write_all(sp[0], payload.data(), payload.size(), nullptr, 2000);
  CHECK(!ok);

  ::close(sp[0]);
}

// -------------------------------------------------------------------------
// read_full returns a short count when the peer closes early (clean EOF).
// -------------------------------------------------------------------------
void test_read_full_short() {
  int sp[2];
  CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);

  constexpr size_t kSent = 100;
  std::thread writer([&] {
    std::vector<std::byte> small(kSent, std::byte{0x5A});
    (void)enet::write_all(sp[0], small.data(), small.size());
    ::close(sp[0]); // EOF for the reader.
  });

  std::vector<std::byte> buf(1000);
  ssize_t const n = enet::read_full(sp[1], buf.data(), buf.size());
  writer.join();

  CHECK(n == static_cast<ssize_t>(kSent));

  ::close(sp[1]);
}

// -------------------------------------------------------------------------
// make_udp_socket + udp_recv over IPv4 loopback.
// -------------------------------------------------------------------------
void test_udp_loopback() {
  int const rx = enet::make_udp_socket(0); // ephemeral port.
  CHECK(rx >= 0);
  uint16_t const port = port_of(rx);
  CHECK(port != 0);

  int const tx = enet::make_udp_socket(0);
  CHECK(tx >= 0);

  const char msg[] = "enet-udp-datagram";
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  dst.sin_port = htons(port);
  ssize_t const sent = ::sendto(tx, msg, sizeof(msg), 0,
                                reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
  CHECK(sent == static_cast<ssize_t>(sizeof(msg)));

  char buf[64] = {};
  sockaddr_in src{};
  ssize_t got = 0;
  for (int tries = 0; tries < 20 && got == 0; ++tries) {
    got = enet::udp_recv(rx, buf, sizeof(buf), src);
  }
  CHECK(got == static_cast<ssize_t>(sizeof(msg)));
  CHECK(std::memcmp(buf, msg, sizeof(msg)) == 0);
  CHECK(src.sin_family == AF_INET);

  ::close(rx);
  ::close(tx);
}

// -------------------------------------------------------------------------
// make_tcp_listener / make_tcp_client round trip with an echo, exercising the
// tcp_socket(int) adopt constructor.  Returns false if the listener could not
// be created for the requested family (used to skip IPv6 where unavailable).
// -------------------------------------------------------------------------
bool tcp_roundtrip(const std::string &addr, int family) {
  enet::tcp_listener_opts lo;
  lo.bind_addr = addr;
  lo.port = 0; // ephemeral.
  lo.family = family;
  int const listener = enet::make_tcp_listener(lo);
  if (listener < 0) {
    return false; // family unavailable on this host.
  }
  uint16_t const port = port_of(listener);
  CHECK(port != 0);

  const std::string request = "ping-over-tcp";

  std::thread server([&] {
    int const conn = ::accept(listener, nullptr, nullptr);
    if (conn < 0) {
      return;
    }
    tcp_socket srv(conn); // adopt constructor from tcp.hpp.
    std::vector<std::byte> in(13);
    ssize_t const r = enet::read_full(srv.sockfd, std::span<std::byte>(in));
    if (r == static_cast<ssize_t>(in.size())) {
      (void)enet::write_all(srv.sockfd, in); // echo back.
    }
    srv.close();
  });

  enet::tcp_connect_opts co;
  co.host = addr;
  co.port = port;
  co.family = family;
  co.tcp_nodelay = true;
  int const cfd = enet::make_tcp_client(co);
  CHECK(cfd >= 0);

  tcp_socket client(cfd); // adopt constructor.
  bool const wrote = enet::write_all(client.sockfd, request);
  CHECK(wrote);

  std::vector<char> echo(request.size());
  ssize_t const rd = enet::read_full(client.sockfd, echo.data(), echo.size());
  CHECK(rd == static_cast<ssize_t>(request.size()));
  CHECK(std::string(echo.data(), echo.size()) == request);

  client.close();
  server.join();
  ::close(listener);
  return true;
}

void test_tcp_roundtrip() {
  CHECK(tcp_roundtrip("127.0.0.1", AF_INET)); // IPv4 must work.

  if (!tcp_roundtrip("::1", AF_INET6)) {
    std::printf("  (IPv6 loopback unavailable -- skipping IPv6 round trip)\n");
  }
}

} // namespace

int main() {
  test_write_read_roundtrip();
  test_write_all_peer_close();
  test_read_full_short();
  test_udp_loopback();
  test_tcp_roundtrip();
  return check_main("socket_io_test");
}
