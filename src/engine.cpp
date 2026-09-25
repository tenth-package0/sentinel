#include "sentinel/engine.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace sentinel {

void SurveillanceEngine::add_rule(std::unique_ptr<Rule> rule) {
  if (!rule) {
    throw std::invalid_argument("rule cannot be null");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  rules_.push_back(std::move(rule));
}

ProcessingResult SurveillanceEngine::process(const Trade& trade) {
  std::lock_guard<std::mutex> lock(mutex_);
  return process_unlocked(trade, true);
}

ProcessingResult SurveillanceEngine::process_unlocked(
    const Trade& trade, bool retain_event) {
  const auto started = std::chrono::steady_clock::now();
  validate(trade);

  ProcessingResult result;
  result.event_id = trade.event_id;

  const auto account_it = positions_.find(trade.account_id);
  if (account_it != positions_.end()) {
    const auto symbol_it = account_it->second.find(trade.symbol);
    if (symbol_it != account_it->second.end()) {
      result.position_after = symbol_it->second;
    }
  }

  if (processed_event_ids_.find(trade.event_id) != processed_event_ids_.end()) {
    result.duplicate = true;
    result.alerts.push_back(
        {"DUPLICATE_EVENT", Severity::Info, "Event was already processed"});
    result.processing_time_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    audit_log_.push_back({trade, result});
    return result;
  }

  const auto latest = latest_event_time_.find(trade.account_id);
  if (latest != latest_event_time_.end() && trade.event_time_ms < latest->second) {
    result.alerts.push_back({"OUT_OF_ORDER_EVENT", Severity::Warning,
                             "Event timestamp is older than the account watermark"});
  }

  const std::int64_t signed_quantity =
      trade.side == Side::Buy ? trade.quantity : -trade.quantity;
  result.position_after += signed_quantity;

  for (const auto& rule : rules_) {
    if (auto alert = rule->evaluate(trade, result.position_after)) {
      result.alerts.push_back(std::move(*alert));
    }
  }

  positions_[trade.account_id][trade.symbol] = result.position_after;
  processed_event_ids_.insert(trade.event_id);
  latest_event_time_[trade.account_id] =
      latest == latest_event_time_.end()
          ? trade.event_time_ms
          : std::max(latest->second, trade.event_time_ms);
  if (retain_event) {
    events_.push_back(trade);
  }

  result.processing_time_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - started)
          .count());
  audit_log_.push_back({trade, result});
  return result;
}

std::vector<ProcessingResult> SurveillanceEngine::replay() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto retained_events = events_;
  reset_state_unlocked(true);

  std::vector<ProcessingResult> results;
  results.reserve(retained_events.size());
  for (const auto& event : retained_events) {
    results.push_back(process_unlocked(event, true));
  }
  return results;
}

void SurveillanceEngine::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  reset_state_unlocked(true);
}

void SurveillanceEngine::reset_state_unlocked(bool clear_events) {
  positions_.clear();
  latest_event_time_.clear();
  processed_event_ids_.clear();
  audit_log_.clear();
  if (clear_events) {
    events_.clear();
  }
}

std::vector<Position> SurveillanceEngine::positions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Position> snapshot;
  for (const auto& [account, symbols] : positions_) {
    for (const auto& [symbol, quantity] : symbols) {
      snapshot.push_back({account, symbol, quantity});
    }
  }
  std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) {
    return left.account_id == right.account_id
               ? left.symbol < right.symbol
               : left.account_id < right.account_id;
  });
  return snapshot;
}

std::vector<AuditRecord> SurveillanceEngine::audit_log() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return audit_log_;
}

std::size_t SurveillanceEngine::processed_event_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return processed_event_ids_.size();
}

void SurveillanceEngine::validate(const Trade& trade) {
  if (trade.event_id.empty() || trade.account_id.empty() || trade.symbol.empty()) {
    throw std::invalid_argument("event_id, account_id, and symbol are required");
  }
  if (trade.quantity <= 0) {
    throw std::invalid_argument("quantity must be positive");
  }
  if (trade.price <= 0.0) {
    throw std::invalid_argument("price must be positive");
  }
  if (trade.event_time_ms < 0) {
    throw std::invalid_argument("event_time_ms cannot be negative");
  }
}

}  // namespace sentinel

