#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>

namespace sentinel {

// Lock-free queue for exactly one producer thread and one consumer thread.
//
// The producer only writes `tail_`, the consumer only writes `head_`, so no
// compare-and-swap is needed: a release store publishes a slot and the
// matching acquire load observes it. Each index lives on its own cache line so
// the two threads do not invalidate each other's line on every operation, and
// each side caches the other's index to avoid reading it on every call.
template <typename T, std::size_t Capacity>
class SpscRing {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two");

 public:
  // Producer thread only.
  bool try_push(const T& value) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - cached_head_ == Capacity) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail - cached_head_ == Capacity) return false;  // Full.
    }
    buffer_[tail & (Capacity - 1)] = value;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Consumer thread only.
  bool try_pop(T& value) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == cached_tail_) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head == cached_tail_) return false;  // Empty.
    }
    value = buffer_[head & (Capacity - 1)];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

 private:
  static constexpr std::size_t kLine = 64;

  alignas(kLine) std::atomic<std::size_t> head_{0};  // Next slot to read.
  std::size_t cached_tail_{0};                        // Consumer's copy of tail_.
  alignas(kLine) std::atomic<std::size_t> tail_{0};  // Next slot to write.
  std::size_t cached_head_{0};                        // Producer's copy of head_.
  alignas(kLine) std::array<T, Capacity> buffer_{};
};

}  // namespace sentinel
