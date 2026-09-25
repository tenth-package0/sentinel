// Differential test: the optimised engine must agree, decision for decision,
// with a deliberately naive model written straight from the specification
// using std::map and std::set. Random streams include duplicates, late
// timestamps, and invalid input.

#include <map>
#include <random>
#include <set>
#include <utility>

#include "sentinel/engine.hpp"
#include "test.hpp"

using namespace sentinel;

namespace {

constexpr std::uint32_t kAccounts = 6;
constexpr std::uint32_t kSymbols = 5;
constexpr SymbolId kRestricted = 4;

class Model {
 public:
  explicit Model(const Config& config) : config_(config) {}

  Decision process(const Trade& t) {
    if (t.id == 0) return {Status::BadId, 0, 0};
    if (t.account >= config_.max_accounts) return {Status::BadAccount, 0, 0};
    if (t.symbol >= config_.max_symbols) return {Status::BadSymbol, 0, 0};
    if (t.quantity < 1 || t.quantity > kMaxQuantity) return {Status::BadQuantity, 0, 0};
    if (t.price < 1 || t.price > kMaxPrice) return {Status::BadPrice, 0, 0};

    std::int64_t& position = positions_[{t.account, t.symbol}];
    if (seen_.count(t.id)) return {Status::Duplicate, 0, position};

    std::uint8_t alerts = 0;
    const auto latest = latest_.find(t.account);
    if (latest != latest_.end() && t.timestamp_ns < latest->second) {
      alerts |= static_cast<std::uint8_t>(Alert::OutOfOrder);
    } else {
      latest_[t.account] = t.timestamp_ns;
    }
    position += t.side == Side::Buy ? t.quantity : -t.quantity;
    if (position > config_.position_limit || position < -config_.position_limit) {
      alerts |= static_cast<std::uint8_t>(Alert::PositionLimit);
    }
    if (t.symbol == kRestricted) alerts |= static_cast<std::uint8_t>(Alert::RestrictedSymbol);
    if (t.quantity * t.price > config_.notional_limit) {
      alerts |= static_cast<std::uint8_t>(Alert::LargeNotional);
    }
    seen_.insert(t.id);
    return {Status::Accepted, alerts, position};
  }

  std::int64_t position(AccountId a, SymbolId s) {
    const auto found = positions_.find({a, s});
    return found == positions_.end() ? 0 : found->second;
  }

 private:
  Config config_;
  std::map<std::pair<AccountId, SymbolId>, std::int64_t> positions_;
  std::map<AccountId, std::int64_t> latest_;
  std::set<EventId> seen_;
};

Trade random_trade(std::mt19937_64& rng, std::int64_t& clock) {
  auto pick = [&](std::uint64_t n) { return rng() % n; };
  Trade t;
  t.id = pick(40'000);                                  // Small range: many duplicates; 0 is invalid.
  t.account = static_cast<AccountId>(pick(kAccounts + 1));  // One past the end is invalid.
  t.symbol = static_cast<SymbolId>(pick(kSymbols + 1));
  t.side = pick(2) ? Side::Buy : Side::Sell;
  t.quantity = pick(200) == 0 ? 0 : static_cast<std::int64_t>(1 + pick(400));
  t.price = pick(200) == 0 ? 0 : static_cast<Price>(1 + pick(2'000 * kPriceScale));
  clock += static_cast<std::int64_t>(pick(1'000));
  t.timestamp_ns = pick(20) == 0 ? clock - static_cast<std::int64_t>(pick(50'000)) : clock;
  return t;
}

void run(std::uint64_t seed) {
  Config config;
  config.max_accounts = kAccounts;
  config.max_symbols = kSymbols;
  config.position_limit = 1'500;
  config.notional_limit = 150'000 * kPriceScale;

  Engine engine(config);
  engine.restrict_symbol(kRestricted);
  Model model(config);

  std::mt19937_64 rng(seed);
  std::int64_t clock = 0;
  int mismatches = 0;
  for (int i = 0; i < 200'000; ++i) {
    const Trade t = random_trade(rng, clock);
    if (!(engine.process(t) == model.process(t))) ++mismatches;
  }
  CHECK(mismatches == 0);

  for (AccountId a = 0; a < kAccounts; ++a) {
    for (SymbolId s = 0; s < kSymbols; ++s) CHECK(engine.position(a, s) == model.position(a, s));
  }
}

}  // namespace

TEST(engine_matches_reference_model) {
  for (std::uint64_t seed : {1, 2, 3, 42, 2026}) run(seed);
}
