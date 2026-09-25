#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "sentinel/types.hpp"

namespace sentinel {

class Rule {
 public:
  virtual ~Rule() = default;
  virtual std::optional<Alert> evaluate(
      const Trade& trade, std::int64_t projected_position) const = 0;
};

class PositionLimitRule final : public Rule {
 public:
  explicit PositionLimitRule(
      std::int64_t default_limit,
      std::unordered_map<std::string, std::int64_t> symbol_limits = {})
      : default_limit_(default_limit), symbol_limits_(std::move(symbol_limits)) {}

  std::optional<Alert> evaluate(
      const Trade& trade, std::int64_t projected_position) const override {
    const auto found = symbol_limits_.find(trade.symbol);
    const auto limit = found == symbol_limits_.end() ? default_limit_ : found->second;
    if (std::llabs(projected_position) <= limit) {
      return std::nullopt;
    }

    std::ostringstream message;
    message << trade.account_id << " would hold " << projected_position << ' '
            << trade.symbol << ", exceeding the absolute limit of " << limit;
    return Alert{"POSITION_LIMIT_BREACH", Severity::Critical, message.str()};
  }

 private:
  std::int64_t default_limit_;
  std::unordered_map<std::string, std::int64_t> symbol_limits_;
};

class RestrictedSymbolRule final : public Rule {
 public:
  explicit RestrictedSymbolRule(std::unordered_set<std::string> restricted)
      : restricted_(std::move(restricted)) {}

  std::optional<Alert> evaluate(
      const Trade& trade, std::int64_t /*projected_position*/) const override {
    if (restricted_.find(trade.symbol) == restricted_.end()) {
      return std::nullopt;
    }
    return Alert{"RESTRICTED_SYMBOL", Severity::Critical,
                 trade.symbol + " is restricted from trading"};
  }

 private:
  std::unordered_set<std::string> restricted_;
};

class LargeNotionalRule final : public Rule {
 public:
  explicit LargeNotionalRule(double threshold) : threshold_(threshold) {}

  std::optional<Alert> evaluate(
      const Trade& trade, std::int64_t /*projected_position*/) const override {
    const double notional = static_cast<double>(trade.quantity) * trade.price;
    if (notional <= threshold_) {
      return std::nullopt;
    }
    std::ostringstream message;
    message << "Trade notional " << notional << " exceeds " << threshold_;
    return Alert{"LARGE_NOTIONAL", Severity::Warning, message.str()};
  }

 private:
  double threshold_;
};

}  // namespace sentinel

