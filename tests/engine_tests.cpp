#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sentinel/engine.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

bool has_alert(const sentinel::ProcessingResult& result, const std::string& code) {
  for (const auto& alert : result.alerts) {
    if (alert.code == code) return true;
  }
  return false;
}

sentinel::Trade trade(std::string id, sentinel::Side side, std::int64_t quantity,
                      std::int64_t time = 1'000, std::string symbol = "AAPL") {
  return {std::move(id), "account-1", std::move(symbol), side, quantity, 100.0,
          time, 1};
}

void add_default_rules(sentinel::SurveillanceEngine& engine) {
  engine.add_rule(std::make_unique<sentinel::PositionLimitRule>(500));
  engine.add_rule(std::make_unique<sentinel::RestrictedSymbolRule>(
      std::unordered_set<std::string>{"LOCK"}));
  engine.add_rule(std::make_unique<sentinel::LargeNotionalRule>(25'000.0));
}

void test_positions_and_sides() {
  sentinel::SurveillanceEngine engine;
  add_default_rules(engine);
  expect(engine.process(trade("one", sentinel::Side::Buy, 200)).position_after == 200,
         "buy increases position");
  expect(engine.process(trade("two", sentinel::Side::Sell, 75)).position_after == 125,
         "sell decreases position");
}

void test_duplicate_is_idempotent() {
  sentinel::SurveillanceEngine engine;
  add_default_rules(engine);
  const auto event = trade("same", sentinel::Side::Buy, 200);
  engine.process(event);
  const auto duplicate = engine.process(event);
  expect(duplicate.duplicate, "duplicate is identified");
  expect(duplicate.position_after == 200, "duplicate does not change position");
  expect(engine.processed_event_count() == 1, "duplicate is not retained twice");
}

void test_rules() {
  sentinel::SurveillanceEngine engine;
  add_default_rules(engine);
  const auto limit = engine.process(trade("limit", sentinel::Side::Buy, 600));
  expect(has_alert(limit, "POSITION_LIMIT_BREACH"), "position limit fires");
  expect(has_alert(limit, "LARGE_NOTIONAL"), "large notional fires");

  const auto restricted =
      engine.process(trade("restricted", sentinel::Side::Buy, 1, 1'100, "LOCK"));
  expect(has_alert(restricted, "RESTRICTED_SYMBOL"), "restricted symbol fires");
}

void test_out_of_order_detection() {
  sentinel::SurveillanceEngine engine;
  add_default_rules(engine);
  engine.process(trade("newer", sentinel::Side::Buy, 1, 2'000));
  const auto older = engine.process(trade("older", sentinel::Side::Buy, 1, 1'500));
  expect(has_alert(older, "OUT_OF_ORDER_EVENT"), "older timestamp is detected");
}

void test_replay_is_deterministic() {
  sentinel::SurveillanceEngine engine;
  add_default_rules(engine);
  engine.process(trade("one", sentinel::Side::Buy, 300));
  engine.process(trade("two", sentinel::Side::Sell, 100, 1'100));
  const auto before = engine.positions();
  const auto replayed = engine.replay();
  const auto after = engine.positions();
  expect(replayed.size() == 2, "replay processes each retained event");
  expect(before.size() == after.size() && before.front().quantity == after.front().quantity,
         "replay rebuilds the same position state");
}

void test_validation() {
  sentinel::SurveillanceEngine engine;
  add_default_rules(engine);
  bool threw = false;
  try {
    engine.process(trade("bad", sentinel::Side::Buy, 0));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  expect(threw, "invalid quantity is rejected");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests{
      {"positions and sides", test_positions_and_sides},
      {"duplicate idempotency", test_duplicate_is_idempotent},
      {"surveillance rules", test_rules},
      {"out-of-order events", test_out_of_order_detection},
      {"deterministic replay", test_replay_is_deterministic},
      {"validation", test_validation},
  };

  for (const auto& [name, test] : tests) {
    test();
    if (failures == 0) std::cout << "PASS: " << name << '\n';
  }
  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
  }
  std::cout << "All tests passed\n";
  return 0;
}
