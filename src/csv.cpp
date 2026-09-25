#include "sentinel/csv.hpp"

#include <array>
#include <charconv>
#include <cstdint>

namespace sentinel {
namespace {

std::string_view trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

template <class Integer>
bool integer(std::string_view text, Integer& value) {
  text = trim(text);
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

bool price(std::string_view text, Price& value) {
  text = trim(text);
  if (text.empty() || text.front() == '-') return false;
  const std::size_t dot = text.find('.');
  const std::string_view whole = dot == std::string_view::npos ? text : text.substr(0, dot);
  std::string_view fraction = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
  if (whole.empty() || fraction.size() > 4 || fraction.find('.') != std::string_view::npos) return false;

  std::uint64_t dollars = 0;
  if (!integer(whole, dollars) || dollars > static_cast<std::uint64_t>(kMaxPrice / kPriceScale)) {
    return false;
  }
  std::uint64_t fractional = 0;
  if (!fraction.empty() && !integer(fraction, fractional)) return false;
  for (std::size_t digits = fraction.size(); digits < 4; ++digits) fractional *= 10;
  if (dollars == static_cast<std::uint64_t>(kMaxPrice / kPriceScale) && fractional != 0) {
    return false;
  }
  value = static_cast<Price>(dollars * kPriceScale + fractional);
  return true;
}

}  // namespace

CsvError parse_trade_csv(std::string_view line, Names& accounts, Names& symbols,
                         CsvTrade& output) {
  std::array<std::string_view, 7> fields;
  std::size_t start = 0;
  for (std::size_t field = 0; field < fields.size(); ++field) {
    const std::size_t comma = line.find(',', start);
    if (field + 1 == fields.size()) {
      if (comma != std::string_view::npos) return CsvError::FieldCount;
      fields[field] = trim(line.substr(start));
    } else {
      if (comma == std::string_view::npos) return CsvError::FieldCount;
      fields[field] = trim(line.substr(start, comma - start));
      start = comma + 1;
    }
  }

  EventId id = 0;
  if (!integer(fields[0], id)) return CsvError::BadEventId;
  if (fields[1].empty()) return CsvError::EmptyAccount;
  if (fields[2].empty()) return CsvError::EmptySymbol;

  Side side;
  if (fields[3] == "BUY") side = Side::Buy;
  else if (fields[3] == "SELL") side = Side::Sell;
  else return CsvError::BadSide;

  std::int64_t quantity = 0;
  if (!integer(fields[4], quantity)) return CsvError::BadQuantity;
  Price parsed_price = 0;
  if (!price(fields[5], parsed_price)) return CsvError::BadPrice;
  std::int64_t timestamp = 0;
  if (!integer(fields[6], timestamp)) return CsvError::BadTimestamp;

  output.account = fields[1];
  output.symbol = fields[2];
  output.trade = {id, quantity, parsed_price, timestamp, accounts.intern(output.account),
                  symbols.intern(output.symbol), side};
  return CsvError::None;
}

const char* to_string(CsvError error) {
  switch (error) {
    case CsvError::None: return "NONE";
    case CsvError::FieldCount: return "FIELD_COUNT";
    case CsvError::BadEventId: return "BAD_EVENT_ID";
    case CsvError::EmptyAccount: return "EMPTY_ACCOUNT";
    case CsvError::EmptySymbol: return "EMPTY_SYMBOL";
    case CsvError::BadSide: return "BAD_SIDE";
    case CsvError::BadQuantity: return "BAD_QUANTITY";
    case CsvError::BadPrice: return "BAD_PRICE";
    case CsvError::BadTimestamp: return "BAD_TIMESTAMP";
  }
  return "UNKNOWN";
}

}  // namespace sentinel
