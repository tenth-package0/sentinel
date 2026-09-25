#pragma once

#include <bit>
#include <cstdint>
#include <vector>

#include "sentinel/types.hpp"

namespace sentinel {

// A resting order in the ITCH book, as much as the decoder needs to attribute
// an execution. 24 bytes; with its key a slot is 32, so two fit per cache line.
struct Order {
  Price price;
  SymbolId symbol;
  AccountId account;
  std::uint32_t shares;
  Side side;
};

// Live orders keyed by ITCH order reference. Reference 0 marks an empty slot,
// so it is never stored or found.
//
// Same layout as IdSet: one flat array, linear probing, doubles at half full.
// A real trading day adds and deletes hundreds of millions of orders, so
// deletion must not leave "tombstones" that slow later lookups. Instead, erase
// uses backward-shift deletion: it pulls later entries of the probe run back
// into the gap, leaving the table exactly as if the key had never been added.
class OrderTable {
 public:
  explicit OrderTable(std::size_t expected = 1 << 20) { rebuild(std::bit_ceil(expected * 2)); }

  Order* find(std::uint64_t ref) {
    if (ref == 0) return nullptr;
    for (std::size_t i = home(ref);; i = next(i)) {
      if (slots_[i].ref == ref) return &slots_[i].order;
      if (slots_[i].ref == 0) return nullptr;
    }
  }

  // Inserts or overwrites.
  void insert(std::uint64_t ref, const Order& order) {
    if (ref == 0) return;
    std::size_t i = home(ref);
    while (slots_[i].ref != 0 && slots_[i].ref != ref) i = next(i);
    if (slots_[i].ref == 0) ++size_;
    slots_[i] = {ref, order};
    if (size_ * 2 > slots_.size()) grow();
  }

  void erase(std::uint64_t ref) {
    if (ref == 0) return;
    std::size_t gap = home(ref);
    while (slots_[gap].ref != ref) {
      if (slots_[gap].ref == 0) return;  // Not present.
      gap = next(gap);
    }
    // Walk the rest of the run. An entry can move back into the gap only if
    // its home slot is not between the gap and where it sits now.
    for (std::size_t i = next(gap); slots_[i].ref != 0; i = next(i)) {
      const std::size_t h = home(slots_[i].ref);
      const bool stays = gap < i ? (gap < h && h <= i) : (gap < h || h <= i);
      if (!stays) {
        slots_[gap] = slots_[i];
        gap = i;
      }
    }
    slots_[gap].ref = 0;
    --size_;
  }

  std::size_t size() const { return size_; }

 private:
  struct Slot {
    std::uint64_t ref;
    Order order;
  };

  std::size_t home(std::uint64_t ref) const {
    return static_cast<std::size_t>((ref * 0x9E3779B97F4A7C15ull) >> shift_);
  }
  std::size_t next(std::size_t i) const { return (i + 1) & (slots_.size() - 1); }

  void rebuild(std::size_t capacity) {
    slots_.assign(capacity, Slot{});
    shift_ = 64 - std::countr_zero(capacity);
    size_ = 0;
  }

  void grow() {
    std::vector<Slot> old = std::move(slots_);
    rebuild(old.size() * 2);
    for (const Slot& slot : old) {
      if (slot.ref != 0) insert(slot.ref, slot.order);
    }
  }

  std::vector<Slot> slots_;
  int shift_{0};
  std::size_t size_{0};
};

}  // namespace sentinel
