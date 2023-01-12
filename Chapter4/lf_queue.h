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

    auto push(const T &t) noexcept {
      store_[next_write_index_] = t;
      next_write_index_ = (next_write_index_ + 1) % store_.size();
    }

    auto getNext() const noexcept -> const T * {
      if (next_read_index_ == next_write_index_)
        return nullptr;

      return &store_[next_read_index_];
    }

    auto pop() noexcept {
      next_read_index_ = (next_read_index_ + 1) % store_.size();
    }

    auto empty() const noexcept {
      return (next_read_index_ == next_write_index_);
    }

    auto size() const noexcept {
      return (next_write_index_ >= next_read_index_) ? (next_write_index_ - next_read_index_) : (store_.size() - next_read_index_ - next_write_index_);
    }

    // Deleted default, copy & move constructors and assignment-operators.
    LFQueue() = delete;
    LFQueue(const LFQueue&) = delete;
    LFQueue(const LFQueue&&) = delete;
    LFQueue& operator=(const LFQueue&) = delete;
    LFQueue& operator=(const LFQueue&&) = delete;

  private:
    std::vector<T> store_;

    std::atomic<size_t> next_write_index_ = 0;
    std::atomic<size_t> next_read_index_ = 0;
  };
}
