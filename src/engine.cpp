#include "sentinel/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace sentinel {

namespace {
constexpr std::int64_t kNoTimestamp = std::numeric_limits<std::int64_t>::min();
constexpr std::uint8_t bit(Alert alert) { return static_cast<std::uint8_t>(alert); }
}  // namespace

Engine::Engine(Config config)
    : config_(config),
      positions_(static_cast<std::size_t>(config.max_accounts) * config.max_symbols, 0),
      watermarks_(config.max_accounts, kNoTimestamp),
      limits_(config.max_symbols, config.position_limit),
      restricted_(config.max_symbols, 0),
      seen_(config.expected_events) {
  if (config.keep_log) log_.reserve(config.expected_events);
}

Decision Engine::process(const Trade& trade) {
  if (const Status invalid = validate(trade); invalid != Status::Accepted) {
    return {invalid, 0, 0};
  }

  std::int64_t& position = positions_[index(trade.account, trade.symbol)];
  if (seen_.contains(trade.id)) {
    if (config_.keep_log) log_.push_back(trade);
    return {Status::Duplicate, 0, position};
  }

  const std::int64_t next =
      position + (trade.side == Side::Buy ? trade.quantity : -trade.quantity);
  if (next > kMaxPosition || next < -kMaxPosition) {
    return {Status::PositionOverflow, 0, position};
  }

  std::uint8_t alerts = 0;
  std::int64_t& watermark = watermarks_[trade.account];
  if (trade.timestamp_ns < watermark) {
    alerts |= bit(Alert::OutOfOrder);
  } else {
    watermark = trade.timestamp_ns;
  }
  if (std::abs(next) > limits_[trade.symbol]) alerts |= bit(Alert::PositionLimit);
  if (restricted_[trade.symbol]) alerts |= bit(Alert::RestrictedSymbol);
  if (trade.quantity * trade.price > config_.notional_limit) alerts |= bit(Alert::LargeNotional);

  position = next;
  seen_.insert(trade.id);
  if (config_.keep_log) log_.push_back(trade);
  return {Status::Accepted, alerts, next};
}

std::vector<Decision> Engine::process_batch(std::span<const Trade> trades) {
  std::vector<Decision> decisions;
  decisions.reserve(trades.size());
  for (const Trade& trade : trades) decisions.push_back(process(trade));
  return decisions;
}

Status Engine::validate(const Trade& trade) const {
  if (trade.id == 0) return Status::BadId;
  if (trade.account >= config_.max_accounts) return Status::BadAccount;
  if (trade.symbol >= config_.max_symbols) return Status::BadSymbol;
  if (trade.quantity <= 0 || trade.quantity > kMaxQuantity) return Status::BadQuantity;
  if (trade.price <= 0 || trade.price > kMaxPrice) return Status::BadPrice;
  return Status::Accepted;
}

bool Engine::set_position_limit(SymbolId symbol, std::int64_t limit) {
  if (symbol >= config_.max_symbols || limit < 0 || limit > kMaxPosition) return false;
  limits_[symbol] = limit;
  return true;
}

bool Engine::set_notional_limit(Price limit) {
  if (limit < 0 || limit > kMaxQuantity * kMaxPrice) return false;
  config_.notional_limit = limit;
  return true;
}

bool Engine::restrict_symbol(SymbolId symbol) {
  if (symbol >= config_.max_symbols) return false;
  restricted_[symbol] = 1;
  return true;
}

bool Engine::allow_symbol(SymbolId symbol) {
  if (symbol >= config_.max_symbols) return false;
  restricted_[symbol] = 0;
  return true;
}

bool Engine::is_restricted(SymbolId symbol) const {
  return symbol < config_.max_symbols && restricted_[symbol] != 0;
}

std::int64_t Engine::position(AccountId account, SymbolId symbol) const {
  if (account >= config_.max_accounts || symbol >= config_.max_symbols) return 0;
  return positions_[index(account, symbol)];
}

std::int64_t Engine::position_limit(SymbolId symbol) const {
  return symbol < config_.max_symbols ? limits_[symbol] : config_.position_limit;
}

std::vector<Position> Engine::positions() const {
  std::vector<Position> result;
  for (AccountId account = 0; account < config_.max_accounts; ++account) {
    for (SymbolId symbol = 0; symbol < config_.max_symbols; ++symbol) {
      if (const std::int64_t quantity = positions_[index(account, symbol)]; quantity != 0) {
        result.push_back({account, symbol, quantity});
      }
    }
  }
  return result;
}

std::size_t Engine::replay() {
  std::vector<Trade> events = std::move(log_);
  reset();
  log_.reserve(events.size());
  for (const Trade& trade : events) process(trade);
  return events.size();
}

void Engine::reset() {
  std::fill(positions_.begin(), positions_.end(), 0);
  std::fill(watermarks_.begin(), watermarks_.end(), kNoTimestamp);
  seen_.clear();
  log_.clear();
}

const char* to_string(Status status) {
  switch (status) {
    case Status::Accepted: return "ACCEPTED";
    case Status::Duplicate: return "DUPLICATE";
    case Status::BadId: return "BAD_ID";
    case Status::BadAccount: return "BAD_ACCOUNT";
    case Status::BadSymbol: return "BAD_SYMBOL";
    case Status::BadQuantity: return "BAD_QUANTITY";
    case Status::BadPrice: return "BAD_PRICE";
    case Status::PositionOverflow: return "POSITION_OVERFLOW";
  }
  return "UNKNOWN";
}

const char* to_string(Alert alert) {
  switch (alert) {
    case Alert::PositionLimit: return "POSITION_LIMIT_BREACH";
    case Alert::RestrictedSymbol: return "RESTRICTED_SYMBOL";
    case Alert::LargeNotional: return "LARGE_NOTIONAL";
    case Alert::OutOfOrder: return "OUT_OF_ORDER_EVENT";
  }
  return "UNKNOWN";
}

const char* to_string(Severity severity) {
  switch (severity) {
    case Severity::None: return "NONE";
    case Severity::Warning: return "WARNING";
    case Severity::Critical: return "CRITICAL";
  }
  return "UNKNOWN";
}

Severity severity_of(Alert alert) {
  return alert == Alert::PositionLimit || alert == Alert::RestrictedSymbol
             ? Severity::Critical
             : Severity::Warning;
}

std::string format_price(Price price) {
  char buffer[32];
  const char* sign = price < 0 ? "-" : "";
  const long long magnitude = std::llabs(price);
  std::snprintf(buffer, sizeof buffer, "%s%lld.%02lld", sign, magnitude / kPriceScale,
                (magnitude % kPriceScale) / 100);
  return buffer;
}

Price parse_price(double dollars) {
  return static_cast<Price>(std::llround(dollars * static_cast<double>(kPriceScale)));
}

std::string explain(Alert alert, const Trade& trade, const Decision& decision,
                    const Engine& engine, const std::string& account,
                    const std::string& symbol) {
  switch (alert) {
    case Alert::PositionLimit:
      return account + " would hold " + std::to_string(decision.position) + ' ' + symbol +
             ", exceeding the absolute limit of " +
             std::to_string(engine.position_limit(trade.symbol));
    case Alert::RestrictedSymbol:
      return symbol + " is restricted from trading";
    case Alert::LargeNotional:
      return "Trade notional $" + format_price(trade.quantity * trade.price) + " exceeds $" +
             format_price(engine.config().notional_limit);
    case Alert::OutOfOrder:
      return "Event timestamp is older than the latest event for " + account;
  }
  return {};
}

}  // namespace sentinel
