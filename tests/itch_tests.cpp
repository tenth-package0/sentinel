#include <string>
#include <vector>

#include "sentinel/itch.hpp"
#include "test.hpp"

using namespace sentinel;

namespace {

// Builds ITCH messages byte by byte, big-endian, as NASDAQ sends them.
class Message {
 public:
  Message(char type, std::uint16_t locate, std::uint64_t timestamp) {
    bytes_.push_back(static_cast<std::uint8_t>(type));
    number(locate, 2);
    number(0, 2);  // Tracking number.
    number(timestamp, 6);
  }
  Message& number(std::uint64_t value, int width) {
    for (int i = width - 1; i >= 0; --i) bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    return *this;
  }
  Message& text(const std::string& value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      bytes_.push_back(static_cast<std::uint8_t>(i < value.size() ? value[i] : ' '));
    }
    return *this;
  }
  Message& byte(char value) { return number(static_cast<std::uint8_t>(value), 1); }
  const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

Message add(std::uint64_t ref, char side, std::uint32_t shares, const std::string& stock,
            std::uint32_t price, std::uint16_t locate = 7) {
  Message m('A', locate, 1'000);
  m.number(ref, 8).byte(side).number(shares, 4).text(stock, 8).number(price, 4);
  return m;
}

Message executed(std::uint64_t ref, std::uint32_t shares, std::uint64_t match,
                 std::uint64_t timestamp = 2'000) {
  Message m('E', 7, timestamp);
  m.number(ref, 8).number(shares, 4).number(match, 8);
  return m;
}

std::optional<Trade> feed(itch::Decoder& decoder, const Message& m) {
  return decoder.decode(m.bytes().data(), m.bytes().size());
}

}  // namespace

TEST(itch_message_sizes_match_the_spec) {
  CHECK(add(1, 'B', 1, "AAPL", 1).bytes().size() == 36);
  CHECK(executed(1, 1, 1).bytes().size() == 31);
}

TEST(itch_execution_becomes_a_trade) {
  itch::Decoder decoder;
  Message directory('R', 7, 0);
  directory.text("AAPL", 8).byte('Q').byte('N').number(100, 4).text("", 19);
  CHECK(!feed(decoder, directory));
  CHECK(decoder.symbol(7) == "AAPL");

  CHECK(!feed(decoder, add(42, 'B', 300, "AAPL", 1'914'200)));
  const auto trade = feed(decoder, executed(42, 100, 9001, 34'200'000'000'000));
  CHECK(trade.has_value());
  CHECK(trade->id == 9001);
  CHECK(trade->symbol == 7);
  CHECK(trade->side == Side::Buy);
  CHECK(trade->quantity == 100);
  CHECK(trade->price == 1'914'200);
  CHECK(trade->timestamp_ns == 34'200'000'000'000);
  CHECK(decoder.accounts().name(trade->account) == "ANON");
  CHECK(decoder.live_orders() == 1);  // 200 shares still resting.

  CHECK(feed(decoder, executed(42, 200, 9002)).has_value());
  CHECK(decoder.live_orders() == 0);  // Fully filled orders are forgotten.
}

TEST(itch_attributed_orders_carry_the_participant) {
  itch::Decoder decoder;
  Message attributed('F', 7, 1'000);
  attributed.number(5, 8).byte('S').number(50, 4).text("MSFT", 8).number(4'181'600, 4).text("GSCO", 4);
  CHECK(attributed.bytes().size() == 40);
  feed(decoder, attributed);
  const auto trade = feed(decoder, executed(5, 50, 1));
  CHECK(trade && decoder.accounts().name(trade->account) == "GSCO");
  CHECK(trade && trade->side == Side::Sell);
}

TEST(itch_cancel_replace_and_delete_update_the_book) {
  itch::Decoder decoder;
  feed(decoder, add(1, 'B', 100, "AAPL", 1'000'000));
  Message cancel('X', 7, 1'500);
  cancel.number(1, 8).number(40, 4);
  feed(decoder, cancel);
  const auto partial = feed(decoder, executed(1, 60, 10));
  CHECK(partial && partial->quantity == 60);
  CHECK(decoder.live_orders() == 0);

  feed(decoder, add(2, 'S', 100, "AAPL", 1'000'000));
  Message replace('U', 7, 1'600);
  replace.number(2, 8).number(3, 8).number(80, 4).number(1'010'000, 4);
  CHECK(replace.bytes().size() == 35);
  feed(decoder, replace);
  CHECK(!feed(decoder, executed(2, 10, 11)));  // Old reference is gone.
  const auto moved = feed(decoder, executed(3, 10, 12));
  CHECK(moved && moved->price == 1'010'000 && moved->side == Side::Sell);

  Message remove('D', 7, 1'700);
  remove.number(3, 8);
  feed(decoder, remove);
  CHECK(!feed(decoder, executed(3, 10, 13)));
}

TEST(itch_execution_with_price_and_hidden_trade) {
  itch::Decoder decoder;
  feed(decoder, add(1, 'B', 100, "AAPL", 1'000'000));
  Message withPrice('C', 7, 2'000);
  withPrice.number(1, 8).number(100, 4).number(20, 8).byte('Y').number(999'900, 4);
  CHECK(withPrice.bytes().size() == 36);
  const auto c = feed(decoder, withPrice);
  CHECK(c && c->price == 999'900 && c->id == 20);

  Message hidden('P', 9, 3'000);
  hidden.number(0, 8).byte('S').number(25, 4).text("NVDA", 8).number(1'783'300, 4).number(21, 8);
  CHECK(hidden.bytes().size() == 44);
  const auto p = feed(decoder, hidden);
  CHECK(p && p->id == 21 && p->symbol == 9 && p->quantity == 25 && p->side == Side::Sell);
}

TEST(itch_truncated_and_unknown_messages_are_ignored) {
  itch::Decoder decoder;
  const Message full = add(1, 'B', 100, "AAPL", 1'000'000);
  for (std::size_t length = 0; length < full.bytes().size(); ++length) {
    CHECK(!decoder.decode(full.bytes().data(), length));
  }
  CHECK(decoder.live_orders() == 0);
  const std::uint8_t unknown[12] = {'Z'};
  CHECK(!decoder.decode(unknown, sizeof unknown));
}

TEST(itch_framing_splits_length_prefixed_messages) {
  std::vector<std::uint8_t> stream;
  for (const Message& m : {add(1, 'B', 1, "A", 1), add(2, 'B', 1, "A", 1)}) {
    stream.push_back(0);
    stream.push_back(static_cast<std::uint8_t>(m.bytes().size()));
    stream.insert(stream.end(), m.bytes().begin(), m.bytes().end());
  }
  const std::size_t whole = stream.size();
  stream.push_back(0);
  stream.push_back(36);
  stream.push_back('A');  // A partial third message.

  int count = 0;
  const std::size_t used =
      itch::for_each_message(stream.data(), stream.size(), [&](const std::uint8_t*, std::size_t n) {
        CHECK(n == 36);
        ++count;
      });
  CHECK(count == 2);
  CHECK(used == whole);
}
