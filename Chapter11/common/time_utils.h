#pragma once

#include <string>
#include <chrono>
#include <ctime>

#include "perf_utils.h"

namespace Common {
  typedef int64_t Nanos;

  constexpr Nanos NANOS_TO_MICROS = 1000;
  constexpr Nanos MICROS_TO_MILLIS = 1000;
  constexpr Nanos MILLIS_TO_SECS = 1000;
  constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS * MICROS_TO_MILLIS;
  constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS * MILLIS_TO_SECS;

  inline auto getCurrentNanos() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  }

  inline auto& getCurrentTimeStr(std::string* time_str) {
    const auto clock = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(clock);

    char nanos_str[24];
    sprintf(nanos_str, "%.8s.%09ld", ctime(&time) + 11, std::chrono::duration_cast<std::chrono::nanoseconds>(clock.time_since_epoch()).count() % NANOS_TO_SECS);
    time_str->assign(nanos_str);

    return *time_str;
  }
}
