#pragma once

#include <cstring>
#include <iostream>

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

inline void ASSERT(bool cond, const std::string& msg) {
  if(UNLIKELY(!cond)) {
    std::cerr << msg << std::endl;
    exit(EXIT_FAILURE);
  }
}

inline void FATAL(const std::string& msg) {
  std::cerr << msg << std::endl;
  exit(EXIT_FAILURE);
}
