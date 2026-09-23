#ifndef ENET_SOCKET_IO_HPP
#define ENET_SOCKET_IO_HPP

// enet/socket_io.hpp --- generic, header-only POSIX socket I/O helpers.
//
// These wrap the raw send()/recv()/recvfrom() syscalls with the loop logic you
// almost always want on a stream or datagram fd: keep going across EINTR, wait
// on poll() for EAGAIN, reassemble short reads/writes, and give up cleanly
// instead of blocking forever.  They operate on plain file descriptors so they
// compose with any socket abstraction (see tcp_listener.hpp / tcp.hpp).
//
// Two cross-cutting mechanisms are worth calling out because they are subtle:
//
//   * The stall deadline (write_all).  A slow or wedged peer can leave the
//     send buffer permanently full, so a naive send-loop would block forever.
//     write_all instead tracks a `last_progress` timestamp that is refreshed
//     every time send() moves at least one byte.  If the wall-clock gap since
//     the last byte moved reaches `stall_deadline_ms`, write_all bails out and
//     returns false.  The deadline measures *time without forward progress*,
//     not total transfer time: a large payload that keeps draining, however
//     slowly, is never cut off, but a connection that stops accepting bytes is.
//
//   * The shutdown-atomic protocol.  Every blocking helper takes an optional
//     `const std::atomic<bool>* shutdown`.  When another thread sets that flag
//     to true it is a cooperative cancel request: the helper notices the flag
//     and returns a failure/short result promptly instead of waiting out the
//     rest of its timeout.  This lets a long-lived I/O call be unblocked
//     without closing the fd out from under it (which would race).  write_all
//     checks the flag at the top of every iteration (relaxed load -- we only
//     need eventual visibility, the atomic itself provides the synchronisation
//     with the writer).  read_full and udp_recv check it whenever a poll()
//     times out, i.e. only while genuinely idle, so an active transfer is not
//     penalised.  Passing nullptr disables the mechanism entirely.

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <span>
#include <type_traits>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace enet {

// Default poll() wait, in milliseconds, while a blocking write is back-pressured
// (send() returned EAGAIN).  Short so the shutdown flag is observed promptly.
inline constexpr int kPollWriteRetryMs = 100;

// Default poll() wait, in milliseconds, while a blocking read has no data ready.
inline constexpr int kPollReadRetryMs = 50;

// Default "time without forward progress" budget for write_all before it gives
// up on a stalled peer.  See the stall-deadline note at the top of the file.
inline constexpr int kWriteStallDeadlineMs = 10000;

// Default socket buffer size requested for UDP sockets created by
// make_udp_socket (8 MiB each way) to tolerate bursty datagram traffic.
inline constexpr int kUdpSockBufBytes = 8 * 1024 * 1024;

// Request `bytes` of kernel send-buffer space (SO_SNDBUF) for `fd`.  Best
// effort: the kernel may clamp the value and any error is ignored, since a
// smaller-than-requested buffer only affects throughput, not correctness.
// (Generalised from phobos' fixed-size set_relay_send_bounds.)
inline void set_send_buffer(int fd, int bytes) noexcept {
  ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes));
}

// Create a non-blocking UDP socket bound to `bind_port` on all interfaces
// (INADDR_ANY).  A port of 0 lets the kernel pick an ephemeral port, which the
// caller can then recover with getsockname().  Both the send and receive
// buffers are enlarged to `buf_bytes`.  Returns the fd, or -1 on failure (the
// socket is closed before returning on error).
[[nodiscard]] inline int make_udp_socket(uint16_t bind_port,
                                         int buf_bytes = kUdpSockBufBytes) noexcept {
  int const fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return -1;
  }
  ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_bytes, sizeof(buf_bytes));
  ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_bytes, sizeof(buf_bytes));

  int const fl = ::fcntl(fd, F_GETFL, 0);
  if (fl >= 0) {
    ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(bind_port);
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Receive a single datagram from a (typically non-blocking) UDP socket, waiting
// up to `poll_ms` for one to arrive.  The sender's address is written to `src`.
//
// Return value:
//    >0  number of bytes received.
//     0  nothing this call -- poll timed out, or a transient error
//        (EAGAIN/EWOULDBLOCK/EINTR).  The caller should loop.
//    -1  the socket is gone (EBADF/ENOTSOCK), poll failed, or a shutdown was
//        requested while idle.  The caller should stop.
[[nodiscard]] inline ssize_t udp_recv(int fd, void *buf, size_t len, sockaddr_in &src,
                                      const std::atomic<bool> *shutdown = nullptr,
                                      int poll_ms = kPollReadRetryMs) noexcept {
  pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
  int const r = ::poll(&pfd, 1, poll_ms);
  if (r < 0) {
    return (errno == EINTR) ? 0 : -1;
  }
  if (r == 0) {
    // Idle: honour a cancel request, otherwise report "nothing yet".
    return ((shutdown != nullptr) && shutdown->load()) ? -1 : 0;
  }

  socklen_t sl = sizeof(src);
  ssize_t const n =
      ::recvfrom(fd, buf, len, MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&src), &sl);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return 0;
    }
    if (errno == EBADF || errno == ENOTSOCK) {
      return -1;
    }
    // Other per-datagram errors (e.g. an ICMP-triggered ECONNREFUSED) are not
    // fatal to the socket -- treat as "nothing this call" and let the caller
    // poll again.
    return 0;
  }
  return n;
}

// Milliseconds between two CLOCK_MONOTONIC timestamps (t1 - t0).
[[nodiscard]] inline long elapsed_ms(const timespec &t0, const timespec &t1) noexcept {
  return ((t1.tv_sec - t0.tv_sec) * 1000) + ((t1.tv_nsec - t0.tv_nsec) / 1000000);
}

// Write exactly `len` bytes from `buf` to a stream socket, reassembling across
// short writes and blocking (via poll) while the send buffer is full.
//
// Returns true once every byte has been handed to the kernel.  Returns false
// if the connection errors (send() fails other than EINTR/EAGAIN, e.g. EPIPE
// after the peer closed), if a shutdown is requested, or if the peer stops
// accepting data for `stall_deadline_ms` without any forward progress.
//
// MSG_NOSIGNAL suppresses SIGPIPE so a peer close surfaces as an EPIPE return
// value rather than a process-killing signal.
[[nodiscard]] inline bool write_all(int fd, const void *buf, size_t len,
                                    const std::atomic<bool> *shutdown = nullptr,
                                    int stall_deadline_ms = kWriteStallDeadlineMs,
                                    int poll_ms = kPollWriteRetryMs) noexcept {
  const auto *p = static_cast<const uint8_t *>(buf);
  timespec last_progress{};
  clock_gettime(CLOCK_MONOTONIC, &last_progress);

  while (len > 0) {
    if ((shutdown != nullptr) && shutdown->load(std::memory_order_relaxed)) {
      return false;
    }

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (elapsed_ms(last_progress, now) >= stall_deadline_ms) {
      return false; // peer has not accepted a byte for too long -- give up.
    }

    ssize_t const n = ::send(fd, p, len, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Send buffer full: wait (briefly) for it to drain, then retry.  A
        // short poll keeps the stall check and shutdown flag responsive.
        pollfd pfd{.fd = fd, .events = POLLOUT, .revents = 0};
        int const r = ::poll(&pfd, 1, poll_ms);
        if (r < 0 && errno != EINTR) {
          return false;
        }
        continue;
      }
      return false; // EPIPE, ECONNRESET, ...
    }

    p += n;
    len -= static_cast<size_t>(n);
    if (n > 0) {
      clock_gettime(CLOCK_MONOTONIC, &last_progress); // record forward progress.
    }
  }
  return true;
}

// Read up to `len` bytes into `buf`, looping until the buffer is full or the
// peer closes the connection.
//
// Return value:
//   ==len  the buffer was filled.
//    <len  the peer closed after sending fewer bytes (a clean short read /
//          EOF); the returned count is how many bytes were received.
//      -1  a socket error occurred, or a shutdown was requested while the read
//          was idle (poll timed out with the flag set).
[[nodiscard]] inline ssize_t read_full(int fd, void *buf, size_t len,
                                       const std::atomic<bool> *shutdown = nullptr,
                                       int poll_ms = kPollReadRetryMs) noexcept {
  auto *p = static_cast<uint8_t *>(buf);
  size_t got = 0;
  while (got < len) {
    ssize_t const n = ::recv(fd, p + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        int const r = ::poll(&pfd, 1, poll_ms);
        if (r < 0 && errno != EINTR) {
          return -1;
        }
        if (r == 0 && (shutdown != nullptr) && shutdown->load()) {
          return -1; // idle and asked to stop.
        }
        continue;
      }
      return -1;
    }
    if (n == 0) {
      return static_cast<ssize_t>(got); // peer closed: report the short count.
    }
    got += static_cast<size_t>(n);
  }
  return static_cast<ssize_t>(got);
}

// A contiguous range of single-byte elements (char, unsigned char, std::byte,
// ...): anything with std::data()/std::size() whose element is one byte wide.
template <class R>
concept ByteRange = requires(R &r) {
  std::data(r);
  std::size(r);
} && sizeof(std::remove_reference_t<decltype(*std::data(std::declval<R &>()))>) == 1;

// Range overload of write_all: writes the whole byte range.
template <ByteRange R>
[[nodiscard]] inline bool write_all(int fd, const R &buf,
                                    const std::atomic<bool> *shutdown = nullptr,
                                    int stall_deadline_ms = kWriteStallDeadlineMs,
                                    int poll_ms = kPollWriteRetryMs) noexcept {
  return write_all(fd, static_cast<const void *>(std::data(buf)), std::size(buf),
                   shutdown, stall_deadline_ms, poll_ms);
}

// Span overload of read_full: fills the whole span (see read_full above for the
// return-value contract).
[[nodiscard]] inline ssize_t read_full(int fd, std::span<std::byte> buf,
                                       const std::atomic<bool> *shutdown = nullptr,
                                       int poll_ms = kPollReadRetryMs) noexcept {
  return read_full(fd, buf.data(), buf.size(), shutdown, poll_ms);
}

} // namespace enet

#endif // ENET_SOCKET_IO_HPP
