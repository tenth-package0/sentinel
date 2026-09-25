#pragma once

#include <cstdint>

namespace sentinel {

// Accounts and symbols are small integers. Text names are mapped to IDs once,
// at the edge of the system (see names.hpp), so the engine never hashes or
// compares strings.
using AccountId = std::uint32_t;
using SymbolId = std::uint32_t;
using EventId = std::uint64_t;

// Prices are integers in 1/10,000 of a dollar, the same scale NASDAQ ITCH uses:
// $191.42 is 1'914'200. Integer money compares exactly and never drifts.
using Price = std::int64_t;
inline constexpr Price kPriceScale = 10'000;

// Input bounds. They guarantee that quantity * price and every position fit in
// an int64 with room to spare, so the hot path needs no overflow checks.
inline constexpr std::int64_t kMaxQuantity = 100'000'000;       // 1e8 shares
inline constexpr Price kMaxPrice = 1'000'000 * kPriceScale;      // $1M per share
inline constexpr std::int64_t kMaxPosition = 1'000'000'000'000;  // 1e12 shares

enum class Side : std::uint8_t { Buy, Sell };

// 48 bytes, trivially copyable: cheap to log, queue, and replay.
struct Trade {
  EventId id{0};  // Must be non-zero and unique per trading session.
  std::int64_t quantity{0};
  Price price{0};
  std::int64_t timestamp_ns{0};
  AccountId account{0};
  SymbolId symbol{0};
  Side side{Side::Buy};
};

enum class Status : std::uint8_t {
  Accepted,   // Applied to positions; `alerts` says what the checks found.
  Duplicate,  // Already seen this event ID; nothing changed.
  // Rejected: the input is malformed and nothing changed.
  BadId,
  BadAccount,
  BadSymbol,
  BadQuantity,
  BadPrice,
  PositionOverflow,
};

// Each check sets one bit, so a decision is a single byte of flags.
enum class Alert : std::uint8_t {
  PositionLimit = 1 << 0,     // |position| is above the symbol's limit.
  RestrictedSymbol = 1 << 1,  // Symbol is on the restricted list.
  LargeNotional = 1 << 2,     // quantity * price is above the threshold.
  OutOfOrder = 1 << 3,        // Timestamp is older than the account's latest.
};

inline constexpr Alert kAllAlerts[] = {Alert::PositionLimit, Alert::RestrictedSymbol,
                                       Alert::LargeNotional, Alert::OutOfOrder};

enum class Severity : std::uint8_t { None, Warning, Critical };

struct Decision {
  Status status{Status::Accepted};
  std::uint8_t alerts{0};   // Bitwise OR of Alert values.
  std::int64_t position{0};  // Position after this trade.
  std::uint64_t policy_version{1};  // Ruleset version used for this decision.

  bool accepted() const { return status == Status::Accepted; }
  bool has(Alert alert) const { return (alerts & static_cast<std::uint8_t>(alert)) != 0; }

  Severity severity() const {
    constexpr auto critical = static_cast<std::uint8_t>(Alert::PositionLimit) |
                              static_cast<std::uint8_t>(Alert::RestrictedSymbol);
    if (alerts & critical) return Severity::Critical;
    return alerts ? Severity::Warning : Severity::None;
  }

  friend bool operator==(const Decision&, const Decision&) = default;
};

}  // namespace sentinel
