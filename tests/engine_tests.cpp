#include "sentinel/engine.hpp"
#include "test.hpp"

using namespace sentinel;

namespace {

constexpr AccountId kAlice = 1;
constexpr SymbolId kAapl = 2;
constexpr SymbolId kLock = 3;

Config small_config() {
  Config config;
  config.max_accounts = 8;
  config.max_symbols = 8;
  config.position_limit = 500;
  config.notional_limit = 25'000 * kPriceScale;
  return config;
}

Trade trade(EventId id, Side side, std::int64_t quantity, std::int64_t time = 1'000,
            SymbolId symbol = kAapl) {
  Trade t;
  t.id = id;
  t.account = kAlice;
  t.symbol = symbol;
  t.side = side;
  t.quantity = quantity;
  t.price = 100 * kPriceScale;
  t.timestamp_ns = time;
  return t;
}

}  // namespace

TEST(buys_and_sells_move_the_position) {
  Engine engine(small_config());
  CHECK(engine.process(trade(1, Side::Buy, 200)).position == 200);
  CHECK(engine.process(trade(2, Side::Sell, 75)).position == 125);
  CHECK(engine.process(trade(3, Side::Sell, 300)).position == -175);
  CHECK(engine.position(kAlice, kAapl) == -175);
}

TEST(batch_processing_matches_one_by_one_processing) {
  const std::vector<Trade> trades = {
      trade(1, Side::Buy, 200),
      trade(2, Side::Sell, 75, 1'100),
      trade(2, Side::Sell, 75, 1'100),
      trade(3, Side::Buy, 600, 900),
  };

  Engine batch(small_config());
  const std::vector<Decision> actual = batch.process_batch(trades);

  Engine one_by_one(small_config());
  std::vector<Decision> expected;
  for (const Trade& event : trades) expected.push_back(one_by_one.process(event));

  CHECK(actual == expected);
  CHECK(batch.positions().size() == one_by_one.positions().size());
  CHECK(batch.position(kAlice, kAapl) == one_by_one.position(kAlice, kAapl));
}

TEST(duplicate_event_changes_nothing) {
  Engine engine(small_config());
  engine.process(trade(7, Side::Buy, 200));
  const Decision again = engine.process(trade(7, Side::Buy, 200));
  CHECK(again.status == Status::Duplicate);
  CHECK(again.position == 200);
  CHECK(engine.position(kAlice, kAapl) == 200);
  CHECK(engine.accepted_count() == 1);
}

TEST(position_limit_is_absolute_and_per_symbol) {
  Engine engine(small_config());
  CHECK(!engine.process(trade(1, Side::Buy, 500)).has(Alert::PositionLimit));  // At the limit.
  CHECK(engine.process(trade(2, Side::Buy, 1)).has(Alert::PositionLimit));    // Over it.
  CHECK(engine.process(trade(3, Side::Sell, 1'100)).has(Alert::PositionLimit));  // -600.

  engine.set_position_limit(kAapl, 10'000);
  CHECK(!engine.process(trade(4, Side::Buy, 1'000)).has(Alert::PositionLimit));
}

TEST(restricted_symbol_is_critical) {
  Engine engine(small_config());
  engine.restrict_symbol(kLock);
  const Decision d = engine.process(trade(1, Side::Buy, 1, 1'000, kLock));
  CHECK(d.accepted());
  CHECK(d.has(Alert::RestrictedSymbol));
  CHECK(d.severity() == Severity::Critical);
}

TEST(policies_can_be_updated_and_inspected_safely) {
  Engine engine(small_config());

  CHECK(engine.set_position_limit(kAapl, 250));
  CHECK(engine.position_limit(kAapl) == 250);
  CHECK(!engine.set_position_limit(kAapl, -1));
  CHECK(!engine.set_position_limit(99, 250));

  CHECK(engine.restrict_symbol(kLock));
  CHECK(engine.is_restricted(kLock));
  CHECK(engine.allow_symbol(kLock));
  CHECK(!engine.is_restricted(kLock));
  CHECK(!engine.restrict_symbol(99));
  CHECK(!engine.allow_symbol(99));

  CHECK(engine.set_notional_limit(10'000 * kPriceScale));
  CHECK(engine.notional_limit() == 10'000 * kPriceScale);
  CHECK(!engine.set_notional_limit(-1));
}

TEST(large_notional_fires_strictly_above_the_threshold) {
  Engine engine(small_config());
  CHECK(!engine.process(trade(1, Side::Buy, 250)).has(Alert::LargeNotional));  // Exactly $25,000.
  const Decision d = engine.process(trade(2, Side::Sell, 251));
  CHECK(d.has(Alert::LargeNotional));
  CHECK(d.severity() == Severity::Warning);
}

TEST(older_timestamp_is_flagged_but_applied) {
  Engine engine(small_config());
  engine.process(trade(1, Side::Buy, 10, 2'000));
  const Decision late = engine.process(trade(2, Side::Buy, 10, 1'500));
  CHECK(late.accepted());
  CHECK(late.has(Alert::OutOfOrder));
  CHECK(late.position == 20);
  // The watermark did not move backwards.
  CHECK(engine.process(trade(3, Side::Buy, 10, 1'800)).has(Alert::OutOfOrder));
}

TEST(invalid_input_is_rejected_without_side_effects) {
  Engine engine(small_config());
  Trade t = trade(1, Side::Buy, 10);

  Trade bad = t;
  bad.id = 0;
  CHECK(engine.process(bad).status == Status::BadId);
  bad = t;
  bad.account = 8;
  CHECK(engine.process(bad).status == Status::BadAccount);
  bad = t;
  bad.symbol = 99;
  CHECK(engine.process(bad).status == Status::BadSymbol);
  bad = t;
  bad.quantity = 0;
  CHECK(engine.process(bad).status == Status::BadQuantity);
  bad.quantity = kMaxQuantity + 1;
  CHECK(engine.process(bad).status == Status::BadQuantity);
  bad = t;
  bad.price = -1;
  CHECK(engine.process(bad).status == Status::BadPrice);

  CHECK(engine.accepted_count() == 0);
  CHECK(engine.log().empty());
  CHECK(engine.process(t).accepted());  // The same ID is still usable.
}

TEST(position_is_bounded) {
  Engine engine(small_config());
  EventId id = 1;
  while (engine.position(kAlice, kAapl) + kMaxQuantity <= kMaxPosition) {
    CHECK(engine.process(trade(id++, Side::Buy, kMaxQuantity)).accepted());
  }
  const Decision d = engine.process(trade(id, Side::Buy, kMaxQuantity));
  CHECK(d.status == Status::PositionOverflow);
  CHECK(engine.process(trade(id, Side::Sell, 1)).accepted());  // ID was not consumed.
}

TEST(replay_rebuilds_identical_state) {
  Engine engine(small_config());
  engine.restrict_symbol(kLock);
  std::vector<Decision> original;
  const Trade events[] = {trade(1, Side::Buy, 300),         trade(2, Side::Sell, 100, 1'100),
                          trade(1, Side::Buy, 300),         trade(3, Side::Buy, 900, 900),
                          trade(4, Side::Buy, 5, 1'200, kLock)};
  for (const Trade& t : events) original.push_back(engine.process(t));
  const auto before = engine.positions();

  CHECK(engine.replay() == 5);
  const auto after = engine.positions();
  CHECK(before.size() == after.size());
  for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
    CHECK(before[i].quantity == after[i].quantity);
  }

  // Decisions are reproducible too: a fresh engine fed the log agrees exactly.
  Engine fresh(small_config());
  fresh.restrict_symbol(kLock);
  for (std::size_t i = 0; i < engine.log().size(); ++i) {
    CHECK(fresh.process(engine.log()[i]) == original[i]);
  }
}

TEST(reset_clears_state_but_keeps_limits) {
  Engine engine(small_config());
  engine.restrict_symbol(kLock);
  engine.process(trade(1, Side::Buy, 100));
  engine.reset();
  CHECK(engine.position(kAlice, kAapl) == 0);
  CHECK(engine.log().empty());
  CHECK(engine.process(trade(1, Side::Buy, 1, 1'000, kLock)).has(Alert::RestrictedSymbol));
}

TEST(id_set_survives_growth) {
  IdSet set(4);
  for (std::uint64_t id = 1; id <= 100'000; ++id) CHECK(set.insert(id * 7919));
  CHECK(set.size() == 100'000);
  bool all_present = true;
  for (std::uint64_t id = 1; id <= 100'000; ++id) all_present &= set.contains(id * 7919);
  CHECK(all_present);
  CHECK(!set.contains(3));
  CHECK(!set.insert(7919));
}

TEST(prices_are_exact_integers) {
  CHECK(parse_price(191.42) == 1'914'200);
  CHECK(parse_price(0.0001) == 1);
  CHECK(format_price(1'914'200) == "191.42");
  CHECK(format_price(250'000 * kPriceScale) == "250000.00");
}

TEST(explanations_name_the_inputs) {
  Engine engine(small_config());
  const Trade t = trade(1, Side::Buy, 600);
  const Decision d = engine.process(t);
  CHECK(explain(Alert::PositionLimit, t, d, engine, "alice", "AAPL") ==
        "alice would hold 600 AAPL, exceeding the absolute limit of 500");
  CHECK(explain(Alert::LargeNotional, t, d, engine, "alice", "AAPL") ==
        "Trade notional $60000.00 exceeds $25000.00");
}
