#include <cstdint>
#include <memory>
#include <thread>

#include "sentinel/spsc_ring.hpp"
#include "test.hpp"

TEST(ring_is_fifo_and_bounded) {
  sentinel::SpscRing<int, 4> ring;
  for (int i = 0; i < 4; ++i) CHECK(ring.try_push(i));
  CHECK(!ring.try_push(99));  // Full.
  int value = -1;
  for (int i = 0; i < 4; ++i) CHECK(ring.try_pop(value) && value == i);
  CHECK(!ring.try_pop(value));  // Empty.
}

TEST(ring_delivers_every_item_in_order_across_threads) {
  constexpr std::uint64_t kItems = 2'000'000;
  auto ring = std::make_unique<sentinel::SpscRing<std::uint64_t, 1024>>();

  std::thread producer([&] {
    for (std::uint64_t i = 1; i <= kItems; ++i) {
      while (!ring->try_push(i)) std::this_thread::yield();
    }
  });

  std::uint64_t expected = 1;
  bool in_order = true;
  while (expected <= kItems) {
    std::uint64_t value = 0;
    if (!ring->try_pop(value)) continue;
    in_order &= value == expected;
    ++expected;
  }
  producer.join();
  CHECK(in_order);
}
