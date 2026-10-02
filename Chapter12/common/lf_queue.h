#pragma once

#include <iostream>
#include <vector>
#include <atomic>
#include <bit>

#include "macros.h"

namespace Common {
  template<typename T>
  class LFQueue final {
  public:
    explicit LFQueue(std::size_t num_elems) :
        store_(num_elems, T()) /* pre-allocation of vector storage. */, index_mask_(num_elems - 1) {
      ASSERT(std::has_single_bit(num_elems), "LFQueue size must be a power of two, got:" + std::to_string(num_elems));
    }

    /// Checks against the writer's cached copy of the read index and only reloads the reader's
    /// cache line when the queue looks full. Writing into a full queue would overwrite unread data.
    auto getNextToWriteTo() noexcept {
      const auto write_index = next_write_index_.load(std::memory_order_relaxed);
      if (UNLIKELY(write_index - cached_read_index_ > index_mask_)) {
        cached_read_index_ = next_read_index_.load(std::memory_order_acquire);
        // Taken once per lap when the cached read index is stale, so build the message only on failure; see updateReadIndex().
        if (UNLIKELY(write_index - cached_read_index_ > index_mask_))
          ASSERT(false, "LFQueue full, size:" + std::to_string(store_.size()));
      }
      return &store_[write_index & index_mask_];
    }

    /// Only the writer thread modifies next_write_index_, so it can read its own index relaxed.
    /// The release store publishes the element written before it to the reader.
    auto updateWriteIndex() noexcept {
      next_write_index_.store(next_write_index_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    /// Checks against the reader's cached copy of the write index and only reloads the writer's
    /// cache line when the queue looks empty.
    /// The acquire load pairs with the release store in updateWriteIndex(), so the element is fully visible.
    auto getNextToRead() const noexcept -> const T * {
      const auto read_index = next_read_index_.load(std::memory_order_relaxed);
      if (read_index == cached_write_index_) {
        const auto write_index = next_write_index_.load(std::memory_order_acquire);
        if (read_index == write_index)
          return nullptr; // Still empty: no store on the polling path, callers spin on this.
        cached_write_index_ = write_index;
      }
      return &store_[read_index & index_mask_];
    }

    /// Only the reader thread modifies next_read_index_.
    /// The release store tells the writer the slot is no longer being read.
    auto updateReadIndex() noexcept {
      const auto read_index = next_read_index_.load(std::memory_order_relaxed);
      // Build the message only on failure: ASSERT is a function, so its message would be built on every pop.
      // See https://github.com/PacktPublishing/Building-Low-Latency-Applications-with-CPP/pull/11
      if (UNLIKELY(read_index == cached_write_index_))
        ASSERT(false, "Read an invalid element in:" + std::to_string(pthread_self()));
      next_read_index_.store(read_index + 1, std::memory_order_release);
    }

    /// Load the read index first: the write index never falls behind it, so the difference cannot underflow.
    auto size() const noexcept {
      const auto read_index = next_read_index_.load(std::memory_order_acquire);
      return next_write_index_.load(std::memory_order_acquire) - read_index;
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    LFQueue() = delete;

    LFQueue(const LFQueue &) = delete;

    LFQueue(const LFQueue &&) = delete;

    LFQueue &operator=(const LFQueue &) = delete;

    LFQueue &operator=(const LFQueue &&) = delete;

  private:
    /// Larger than a 64-byte cache line because Intel's spatial prefetcher pulls lines in adjacent pairs.
    static constexpr size_t CACHE_LINE_SIZE = 128;

    /// Underlying container of data accessed in FIFO order.
    /// Read-only after construction, so it must not share a cache line with the indices written below.
    std::vector<T> store_;

    /// store_.size() - 1. The size is a power of two, so index & mask wraps an index without a division.
    const size_t index_mask_;

    /// Atomic trackers for next index to write new data to and read new data from.
    /// Each lives on its own cache line so the writer and the reader do not invalidate each other's line.
    /// The indices only ever increase and are wrapped on access with index_mask_, so size() is their difference
    /// and no counter has to be shared between the two threads.
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_write_index_ = {0};
    /// Writer-private copy of next_read_index_, on the writer's cache line.
    size_t cached_read_index_ = 0;

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> next_read_index_ = {0};
    /// Reader-private copy of next_write_index_, on the reader's cache line.
    mutable size_t cached_write_index_ = 0;
  };
}
