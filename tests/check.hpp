#ifndef ENET_TESTS_CHECK_HPP
#define ENET_TESTS_CHECK_HPP

// Minimal test harness shared by the enet unit tests: CHECK records a failure
// (file:line + expression) and keeps going; check_main() returns non-zero if
// any check failed, so `make test` stops on the first failing binary.

#include <cstdio>

namespace check_detail {
inline int failures = 0;
inline int checks = 0;
} // namespace check_detail

#define CHECK(expr)                                                            \
  do {                                                                         \
    ++check_detail::checks;                                                    \
    if (!(expr)) {                                                             \
      ++check_detail::failures;                                                \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,    \
                   #expr);                                                     \
    }                                                                          \
  } while (0)

inline int check_main(const char *name) {
  std::printf("%s: %d/%d checks passed\n", name,
              check_detail::checks - check_detail::failures,
              check_detail::checks);
  return check_detail::failures == 0 ? 0 : 1;
}

#endif
