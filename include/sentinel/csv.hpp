#pragma once

#include <string>
#include <string_view>

#include "sentinel/names.hpp"
#include "sentinel/types.hpp"

namespace sentinel {

enum class CsvError {
  None,
  FieldCount,
  BadEventId,
  EmptyAccount,
  EmptySymbol,
  BadSide,
  BadQuantity,
  BadPrice,
  BadTimestamp,
};

struct CsvTrade {
  Trade trade;
  std::string account;
  std::string symbol;
};

// Parses: event_id,account,symbol,side,quantity,price,timestamp_ns
// Fields are deliberately unquoted; this adapter is intended for fast,
// machine-generated ingestion rather than general spreadsheet CSV.
CsvError parse_trade_csv(std::string_view line, Names& accounts, Names& symbols,
                         CsvTrade& output);
const char* to_string(CsvError error);

}  // namespace sentinel
