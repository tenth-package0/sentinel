#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace sentinel {

// Hash set of non-zero 64-bit IDs, used to reject duplicate events.
//
// Open addressing with linear probing: the table is one flat array, a lookup
// hashes to a slot and scans forward until it finds the key or an empty slot
// (0). Neighbouring slots share cache lines, so most lookups touch one line.
// The table doubles when half full, which keeps probe sequences short.
class IdSet {
 public:
  explicit IdSet(std::size_t expected = 1024) { rebuild(std::bit_ceil(expected * 2)); }

  bool contains(std::uint64_t id) const {
    for (std::size_t i = slot(id);; i = (i + 1) & mask_) {
      if (slots_[i] == id) return true;
      if (slots_[i] == 0) return false;
    }
  }

  // Returns false if the ID was already present.
  bool insert(std::uint64_t id) {
    std::size_t i = slot(id);
    for (; slots_[i] != 0; i = (i + 1) & mask_) {
      if (slots_[i] == id) return false;
    }
    slots_[i] = id;
    if (++size_ * 2 > slots_.size()) grow();
    return true;
  }

  void clear() {
    std::fill(slots_.begin(), slots_.end(), 0);
    size_ = 0;
  }

  std::size_t size() const { return size_; }

 private:
  // Fibonacci hashing: multiply by 2^64 / golden ratio and keep the top bits.
  // Spreads sequential IDs evenly across the table.
  std::size_t slot(std::uint64_t id) const {
    return static_cast<std::size_t>((id * 0x9E3779B97F4A7C15ull) >> shift_);
  }

  void rebuild(std::size_t capacity) {
    slots_.assign(capacity, 0);
    mask_ = capacity - 1;
    shift_ = 64 - std::countr_zero(capacity);
    size_ = 0;
  }

  void grow() {
    std::vector<std::uint64_t> old = std::move(slots_);
    rebuild(old.size() * 2);
    for (std::uint64_t id : old) {
      if (id != 0) insert(id);
    }
  }

  std::vector<std::uint64_t> slots_;
  std::size_t mask_{0};
  int shift_{0};
  std::size_t size_{0};
};

}  // namespace sentinel
