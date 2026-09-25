#include "sentinel/itch.hpp"

namespace sentinel::itch {

namespace {

// ITCH integers are big-endian and unaligned.
std::uint64_t read(const std::uint8_t* p, int bytes) {
  std::uint64_t value = 0;
  for (int i = 0; i < bytes; ++i) value = (value << 8) | p[i];
  return value;
}

std::string trim(const std::uint8_t* p, std::size_t length) {
  std::string text(reinterpret_cast<const char*>(p), length);
  text.erase(text.find_last_not_of(' ') + 1);
  return text;
}

// Every message starts with: type(1) stock_locate(2) tracking(2) timestamp(6).
constexpr std::size_t kLocate = 1;
constexpr std::size_t kTimestamp = 5;
constexpr std::size_t kBody = 11;

}  // namespace

Decoder::Decoder() : anonymous_(accounts_.intern("ANON")) { orders_.reserve(1 << 20); }

std::string Decoder::symbol(SymbolId locate) const {
  return locate < symbols_.size() && !symbols_[locate].empty() ? symbols_[locate]
                                                                : "#" + std::to_string(locate);
}

std::optional<Trade> Decoder::decode(const std::uint8_t* m, std::size_t length) {
  if (length < kBody) return std::nullopt;
  const auto locate = static_cast<SymbolId>(read(m + kLocate, 2));
  const auto timestamp = static_cast<std::int64_t>(read(m + kTimestamp, 6));
  const std::uint8_t* body = m + kBody;

  switch (m[0]) {
    case 'R':  // Stock directory: locate code -> ticker.
      if (length < 19) break;
      if (symbols_.size() <= locate) symbols_.resize(locate + 1);
      symbols_[locate] = trim(body, 8);
      break;

    case 'A':  // Add order, anonymous.
    case 'F':  // Add order, attributed to a participant.
      if (length < (m[0] == 'F' ? 40u : 36u)) break;
      add(read(body, 8),
          {locate,
           m[0] == 'F' ? accounts_.intern(trim(body + 25, 4)) : anonymous_,
           body[8] == 'B' ? Side::Buy : Side::Sell,
           static_cast<Price>(read(body + 21, 4)),
           static_cast<std::uint32_t>(read(body + 9, 4))});
      break;

    case 'E':  // Order executed at its limit price.
      if (length < 31) break;
      return execute(read(body, 8), static_cast<std::uint32_t>(read(body + 8, 4)), 0,
                     read(body + 12, 8), timestamp);

    case 'C':  // Order executed at a different price.
      if (length < 36) break;
      return execute(read(body, 8), static_cast<std::uint32_t>(read(body + 8, 4)),
                     static_cast<Price>(read(body + 21, 4)), read(body + 12, 8), timestamp);

    case 'X':  // Partial cancel.
      if (length < 23) break;
      reduce(read(body, 8), static_cast<std::uint32_t>(read(body + 8, 4)));
      break;

    case 'D':  // Delete.
      if (length < 19) break;
      orders_.erase(read(body, 8));
      break;

    case 'U': {  // Replace: new reference, shares, and price; same side and participant.
      if (length < 35) break;
      const auto found = orders_.find(read(body, 8));
      if (found == orders_.end()) break;
      Order order = found->second;
      orders_.erase(found);
      order.shares = static_cast<std::uint32_t>(read(body + 16, 4));
      order.price = static_cast<Price>(read(body + 20, 4));
      add(read(body + 8, 8), order);
      break;
    }

    case 'P': {  // Trade against a hidden order; no order reference or participant.
      if (length < 44) break;
      Trade trade;
      trade.id = read(body + 25, 8);
      trade.account = anonymous_;
      trade.symbol = locate;
      trade.side = body[8] == 'B' ? Side::Buy : Side::Sell;
      trade.quantity = static_cast<std::int64_t>(read(body + 9, 4));
      trade.price = static_cast<Price>(read(body + 21, 4));
      trade.timestamp_ns = timestamp;
      return trade;
    }
  }
  return std::nullopt;
}

void Decoder::add(std::uint64_t reference, const Order& order) {
  if (order.shares > 0) orders_[reference] = order;
}

std::optional<Trade> Decoder::execute(std::uint64_t reference, std::uint32_t shares,
                                      Price price, std::uint64_t match,
                                      std::int64_t timestamp) {
  const auto found = orders_.find(reference);
  if (found == orders_.end()) return std::nullopt;
  const Order& order = found->second;

  Trade trade;
  trade.id = match;
  trade.account = order.account;
  trade.symbol = order.symbol;
  trade.side = order.side;
  trade.quantity = shares;
  trade.price = price != 0 ? price : order.price;
  trade.timestamp_ns = timestamp;
  reduce(reference, shares);
  return trade;
}

void Decoder::reduce(std::uint64_t reference, std::uint32_t shares) {
  const auto found = orders_.find(reference);
  if (found == orders_.end()) return;
  if (found->second.shares <= shares) {
    orders_.erase(found);
  } else {
    found->second.shares -= shares;
  }
}

}  // namespace sentinel::itch
