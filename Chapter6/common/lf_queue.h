#pragma once

#include <iostream>
#include <vector>
#include <atomic>

namespace Common {
  template<typename T>
  class LFQueue final {
  public:
    LFQueue(std::size_t num_elems) :
        store_(num_elems, T()) /* pre-allocation of vector storage. */ {
    }

    auto getNextToWriteTo() noexcept {
      return &store_[next_write_index_];
    }

    auto updateWriteIndex() noexcept {
      next_write_index_ = (next_write_index_ + 1) % store_.size();
    }

    auto getNextToRead() const noexcept -> const T * {
      return (next_read_index_ == next_write_index_) ? nullptr : &store_[next_read_index_];
    }

    auto updateReadIndex() noexcept {
      next_read_index_ = (next_read_index_ + 1) % store_.size();
    }

    auto empty() const noexcept {
      return (next_read_index_ == next_write_index_);
    }

    // Deleted default, copy & move constructors and assignment-operators.
    LFQueue() = delete;

    LFQueue(const LFQueue &) = delete;

    LFQueue(const LFQueue &&) = delete;

    LFQueue &operator=(const LFQueue &) = delete;

    LFQueue &operator=(const LFQueue &&) = delete;

  private:
    std::vector<T> store_;

    std::atomic<size_t> next_write_index_ = 0;
    std::atomic<size_t> next_read_index_ = 0;
  };
}
