#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sentinel/id_set.hpp"
#include "sentinel/types.hpp"

namespace sentinel {

struct Config {
  // Account and symbol IDs must be below these. State is preallocated from
  // them, so processing a trade never allocates (other than growing the log).
  std::uint32_t max_accounts = 1024;
  std::uint32_t max_symbols = 1024;

  // Reserve room for this many trades up front (seen IDs and the log).
  std::size_t expected_events = 1024;

  // Keep every trade for replay(). Turn off for long streams where the source
  // (for example an ITCH file) is itself the replayable log.
  bool keep_log = true;

  std::int64_t position_limit = 1'000;             // Absolute shares, per account and symbol.
  Price notional_limit = 250'000 * kPriceScale;    // quantity * price, in price units.
};

struct Position {
  AccountId account;
  SymbolId symbol;
  std::int64_t quantity;
};

// Single-threaded, deterministic trade-surveillance engine.
//
// One thread owns all state, so there are no locks. Feed it from other threads
// through an SpscRing (see spsc_ring.hpp). The same sequence of trades always
// produces the same decisions and positions, which is what makes replay work.
class Engine {
 public:
  explicit Engine(Config config = {});

  // The hot path: validate, reject duplicates, run the checks, update the position.
  Decision process(const Trade& trade);

  // Processes a contiguous event batch in arrival order. This keeps ordering
  // semantics identical to repeated process() calls while giving adapters one
  // call that returns the complete decision sequence.
  std::vector<Decision> process_batch(std::span<const Trade> trades);

  bool set_position_limit(SymbolId symbol, std::int64_t limit);
  bool set_notional_limit(Price limit);
  bool restrict_symbol(SymbolId symbol);
  bool allow_symbol(SymbolId symbol);

  std::int64_t position(AccountId account, SymbolId symbol) const;
  std::int64_t position_limit(SymbolId symbol) const;
  Price notional_limit() const { return config_.notional_limit; }
  bool is_restricted(SymbolId symbol) const;
  std::uint64_t policy_version() const { return policy_version_; }
  std::vector<Position> positions() const;  // Non-zero positions, sorted by account then symbol.
  std::size_t accepted_count() const { return seen_.size(); }
  const Config& config() const { return config_; }

  // Every trade that passed validation, in arrival order (duplicates included).
  const std::vector<Trade>& log() const { return log_; }

  // Clears positions and rebuilds them by re-processing the log. Returns the
  // number of trades replayed.
  std::size_t replay();

  // Clears positions, watermarks, seen IDs, and the log. Limits are kept.
  void reset();

 private:
  Status validate(const Trade& trade) const;
  std::size_t index(AccountId account, SymbolId symbol) const {
    return static_cast<std::size_t>(account) * config_.max_symbols + symbol;
  }

  Config config_;
  std::vector<std::int64_t> positions_;     // [account * max_symbols + symbol]
  std::vector<std::int64_t> watermarks_;    // Latest timestamp seen, per account.
  std::vector<std::int64_t> limits_;        // Position limit, per symbol.
  std::vector<std::uint8_t> restricted_;    // 1 if restricted, per symbol.
  IdSet seen_;
  std::vector<Trade> log_;
  std::uint64_t policy_version_{1};
};

// Off the hot path: text for display, logs, and the web dashboard.
const char* to_string(Status status);
const char* to_string(Alert alert);       // e.g. "POSITION_LIMIT_BREACH"
const char* to_string(Severity severity);
Severity severity_of(Alert alert);
std::string explain(Alert alert, const Trade& trade, const Decision& decision,
                    const Engine& engine, const std::string& account,
                    const std::string& symbol);
std::string format_price(Price price);    // 1914200 -> "191.42"
Price parse_price(double dollars);        // 191.42 -> 1914200 (rounded to the nearest unit)

}  // namespace sentinel
