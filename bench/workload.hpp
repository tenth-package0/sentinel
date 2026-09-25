#pragma once

// The benchmark workload, shared by the native benchmark and the in-browser
// stress test so both measure exactly the same thing.

#include <random>
#include <vector>

#include "sentinel/engine.hpp"

namespace sentinel::bench {

inline constexpr std::uint32_t kAccounts = 256;
inline constexpr std::uint32_t kSymbols = 1024;

inline Config config(std::size_t events) {
  Config c;
  c.max_accounts = kAccounts;
  c.max_symbols = kSymbols;
  c.expected_events = events;  // Preallocate so the timed loop never allocates.
  c.keep_log = false;
  return c;
}

// Random trades across 256 accounts and 1,024 symbols, with ~1% duplicate
// deliveries and ~1% late timestamps. Deterministic for a given seed.
inline std::vector<Trade> trades(std::size_t count, std::uint64_t seed = 7) {
  std::mt19937_64 rng(seed);
  std::vector<Trade> out(count);
  std::int64_t clock = 34'200'000'000'000;  // 09:30 in ns since midnight.
  for (std::size_t i = 0; i < count; ++i) {
    Trade& t = out[i];
    const bool duplicate = i > 1000 && rng() % 100 == 0;
    t.id = duplicate ? out[i - 1 - rng() % 1000].id : i + 1;
    t.account = static_cast<AccountId>(rng() % kAccounts);
    t.symbol = static_cast<SymbolId>(rng() % kSymbols);
    t.side = rng() % 2 ? Side::Buy : Side::Sell;
    t.quantity = static_cast<std::int64_t>(1 + rng() % 500);
    t.price = static_cast<Price>((10 + rng() % 490) * kPriceScale);
    clock += 1'000;
    t.timestamp_ns = rng() % 100 == 0 ? clock - 5'000'000 : clock;
  }
  return out;
}

}  // namespace sentinel::bench
