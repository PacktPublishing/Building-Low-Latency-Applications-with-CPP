#pragma once

namespace Common {
  /// Read from the TSC register and return a uint64_t value to represent elapsed CPU clock cycles.
  /// Not ordered with surrounding instructions: the CPU may read the counter before earlier
  /// instructions finish. Use rdtscStart() / rdtscEnd() to time a region.
  inline auto rdtsc() noexcept {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t) hi << 32) | lo;
  }

  /// Read the TSC at the start of a timed region (Intel SDM: LFENCE; RDTSC; LFENCE).
  /// Earlier instructions finish before the read, and the timed code does not start before it.
  /// The "memory" clobber keeps the compiler from moving loads and stores across it.
  inline auto rdtscStart() noexcept {
    unsigned int lo, hi;
    __asm__ __volatile__ ("lfence\n\trdtsc\n\tlfence" : "=a" (lo), "=d" (hi) :: "memory");
    return ((uint64_t) hi << 32) | lo;
  }

  /// Read the TSC at the end of a timed region (Intel SDM: RDTSCP; LFENCE).
  /// RDTSCP waits for the timed code, including its loads, to finish; LFENCE keeps later code
  /// from starting before the read. RDTSCP also writes ECX (TSC_AUX), hence the clobber.
  inline auto rdtscEnd() noexcept {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtscp\n\tlfence" : "=a" (lo), "=d" (hi) :: "rcx", "memory");
    return ((uint64_t) hi << 32) | lo;
  }
}

/// Start latency measurement using rdtsc(). Creates a variable called TAG in the local scope.
#define START_MEASURE(TAG) const auto TAG = Common::rdtscStart()

/// End latency measurement using rdtsc(). Expects a variable called TAG to already exist in the local scope.
#define END_MEASURE(TAG, LOGGER)                                                              \
      do {                                                                                    \
        const auto end = Common::rdtscEnd();                                                  \
        LOGGER.log("% RDTSC "#TAG" %\n", Common::getCurrentTimeStr(&time_str_), (end - TAG)); \
      } while(false)

/// Log a current timestamp at the time this macro is invoked.
#define TTT_MEASURE(TAG, LOGGER)                                                              \
      do {                                                                                    \
        const auto TAG = Common::getCurrentNanos();                                           \
        LOGGER.log("% TTT "#TAG" %\n", Common::getCurrentTimeStr(&time_str_), TAG);           \
      } while(false)
