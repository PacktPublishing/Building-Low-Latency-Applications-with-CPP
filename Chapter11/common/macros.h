#pragma once

#include <cstring>
#include <iostream>

/// Branch prediction hints.
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

/// Check condition and exit if not true.
inline auto ASSERT(bool cond, const std::string &msg) noexcept {
  if (UNLIKELY(!cond)) {
    std::cerr << "ASSERT : " << msg << std::endl;

    exit(EXIT_FAILURE);
  }
}

inline auto FATAL(const std::string &msg) noexcept {
  std::cerr << "FATAL : " << msg << std::endl;

  exit(EXIT_FAILURE);
}
