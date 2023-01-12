#pragma once

#include <cstdint>
#include <vector>

namespace Common {
  template<typename T>
  class MemPool final {
  public:
    explicit MemPool(std::size_t num_elems) :
        store_(num_elems, {T(), true}) /* pre-allocation of vector storage. */ {
    }

    template<typename... Args>
    T *allocate(Args... args) noexcept {
      auto *obj_block = &(store_[next_free_index_]);
      T *ret = &(obj_block->object_);
      ret = new(ret) T(args...); // placement new.
      obj_block->is_free_ = false;

      updateNextFreeIndex();

      return ret;
    }

    auto deallocate(const T __restrict *elem) noexcept {
      // so that the next allocation tries to use this element to attempt better cache locality.
      next_free_index_ = (reinterpret_cast<const ObjectBlock *>(elem) - &store_[0]);
      store_[next_free_index_].is_free_ = true;
    }

    auto updateNextFreeIndex() noexcept {
      while (!store_[next_free_index_].is_free_) {
        ++next_free_index_;
        if(UNLIKELY(next_free_index_ == store_.size())) { // hardware branch predictor should almost always predict this to be false any ways.
          next_free_index_ = 0;
        }
      }
    }

    // Deleted default, copy & move constructors and assignment-operators.
    MemPool() = delete;
    MemPool(const MemPool&) = delete;
    MemPool(const MemPool&&) = delete;
    MemPool& operator=(const MemPool&) = delete;
    MemPool& operator=(const MemPool&&) = delete;

  private:
    // It is better to have one vector of structs with two objects than two vectors of one object.
    // Consider how these are accessed and cache performance.
    struct ObjectBlock {
      T object_;
      bool is_free_;
    };

    // We could've chosen to use a std::array that would allocate the memory on the stack instead of the heap.
    // We would have to measure to see which one yields better performance.
    // It is good to have objects on the stack but performance starts getting worse as the size of the pool increases.
    std::vector<ObjectBlock> store_;

    size_t next_free_index_ = 0;
  };
}
