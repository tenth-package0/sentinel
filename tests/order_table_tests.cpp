#include <random>
#include <unordered_map>

#include "sentinel/order_table.hpp"
#include "test.hpp"

using sentinel::Order;
using sentinel::OrderTable;

// Differential test: a million random inserts, updates, and erases on a small
// key range (long probe runs, lots of wrap-around) must match std::unordered_map.
TEST(order_table_matches_unordered_map) {
  OrderTable table(4);
  std::unordered_map<std::uint64_t, std::uint32_t> reference;
  std::mt19937_64 rng(11);
  int mismatches = 0;
  for (int step = 0; step < 1'000'000; ++step) {
    const std::uint64_t key = 1 + rng() % 5'000;
    if (rng() % 2) {
      const auto shares = static_cast<std::uint32_t>(rng());
      table.insert(key, Order{0, 0, 0, shares, sentinel::Side::Buy});
      reference[key] = shares;
    } else {
      table.erase(key);
      reference.erase(key);
    }
    const Order* found = table.find(key);
    const auto expected = reference.find(key);
    if ((found != nullptr) != (expected != reference.end())) ++mismatches;
    if (found && expected != reference.end() && found->shares != expected->second) ++mismatches;
  }
  CHECK(mismatches == 0);
  CHECK(table.size() == reference.size());
  for (const auto& [key, shares] : reference) {
    const Order* found = table.find(key);
    CHECK(found && found->shares == shares);
  }
}

TEST(order_table_ignores_reference_zero) {
  OrderTable table;
  table.insert(0, Order{});
  CHECK(table.find(0) == nullptr);
  CHECK(table.size() == 0);
  table.erase(0);
}
