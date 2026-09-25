#include <iomanip>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sentinel/engine.hpp"

namespace {

void print_result(const sentinel::Trade& trade,
                  const sentinel::ProcessingResult& result) {
  std::cout << std::left << std::setw(8) << trade.event_id << std::setw(7)
            << sentinel::to_string(trade.side) << std::setw(7) << trade.symbol
            << " position=" << std::setw(6) << result.position_after
            << " duplicate=" << std::boolalpha << result.duplicate << '\n';
  for (const auto& alert : result.alerts) {
    std::cout << "  [" << sentinel::to_string(alert.severity) << "] "
              << alert.code << ": " << alert.message << '\n';
  }
}

}  // namespace

int main() {
  sentinel::SurveillanceEngine engine;
  engine.add_rule(std::make_unique<sentinel::PositionLimitRule>(
      1'000, std::unordered_map<std::string, std::int64_t>{{"AAPL", 500}}));
  engine.add_rule(std::make_unique<sentinel::RestrictedSymbolRule>(
      std::unordered_set<std::string>{"LOCK"}));
  engine.add_rule(std::make_unique<sentinel::LargeNotionalRule>(250'000.0));

  const std::vector<sentinel::Trade> trades{
      {"evt-1", "alpha", "AAPL", sentinel::Side::Buy, 300, 190.0, 1'000, 1},
      {"evt-2", "alpha", "AAPL", sentinel::Side::Buy, 250, 191.0, 1'010, 2},
      {"evt-3", "bravo", "LOCK", sentinel::Side::Sell, 40, 55.0, 1'020, 3},
      {"evt-4", "alpha", "MSFT", sentinel::Side::Buy, 800, 420.0, 990, 4},
      {"evt-1", "alpha", "AAPL", sentinel::Side::Buy, 300, 190.0, 1'000, 1},
  };

  std::cout << "Sentinel trade-surveillance demo\n\n";
  for (const auto& trade : trades) {
    print_result(trade, engine.process(trade));
  }

  std::cout << "\nPositions\n";
  for (const auto& position : engine.positions()) {
    std::cout << "  " << position.account_id << '/' << position.symbol << ": "
              << position.quantity << '\n';
  }
}

