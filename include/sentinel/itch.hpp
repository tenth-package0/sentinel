#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sentinel/names.hpp"
#include "sentinel/types.hpp"

namespace sentinel::itch {

// Decodes NASDAQ TotalView-ITCH 5.0 messages into executed trades.
//
// ITCH is the exchange's public order-by-order feed. Executions ('E', 'C')
// refer back to an earlier order by reference number, so the decoder keeps a
// table of live orders to recover each execution's symbol, side, price, and
// participant. Each execution becomes one Trade:
//
//   account  = the resting order's market participant (MPID) for attributed
//              orders ('F'), otherwise the shared "ANON" account
//   side     = the resting order's side, i.e. what that participant did
//   symbol   = ITCH's stock locate code, already a dense integer ID
//   id       = ITCH's match number, unique for the day
//
// Specification: https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
class Decoder {
 public:
  Decoder();

  // `message` points at one message without the 2-byte length prefix used in
  // NASDAQ's files. Malformed or truncated messages are ignored.
  std::optional<Trade> decode(const std::uint8_t* message, std::size_t length);

  // Participants seen so far; AccountId values index into this.
  const Names& accounts() const { return accounts_; }

  // Ticker for a stock locate code, from the stock directory ('R') messages.
  std::string symbol(SymbolId locate) const;

  std::size_t live_orders() const { return orders_.size(); }

 private:
  struct Order {
    SymbolId symbol;
    AccountId account;
    Side side;
    Price price;
    std::uint32_t shares;
  };

  void add(std::uint64_t reference, const Order& order);
  std::optional<Trade> execute(std::uint64_t reference, std::uint32_t shares, Price price,
                               std::uint64_t match, std::int64_t timestamp);
  void reduce(std::uint64_t reference, std::uint32_t shares);

  Names accounts_;
  AccountId anonymous_;
  std::unordered_map<std::uint64_t, Order> orders_;
  std::vector<std::string> symbols_;
};

// Splits a buffer of length-prefixed ITCH messages (NASDAQ's file format:
// 2-byte big-endian length, then the message). Returns bytes consumed; a
// trailing partial message is left for the next call.
template <typename OnMessage>
std::size_t for_each_message(const std::uint8_t* data, std::size_t size, OnMessage&& on_message) {
  std::size_t offset = 0;
  while (size - offset >= 2) {
    const std::size_t length = (std::size_t{data[offset]} << 8) | data[offset + 1];
    if (size - offset - 2 < length) break;
    on_message(data + offset + 2, length);
    offset += 2 + length;
  }
  return offset;
}

}  // namespace sentinel::itch
