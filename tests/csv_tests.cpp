#include "sentinel/csv.hpp"
#include "test.hpp"

using namespace sentinel;

TEST(csv_trade_parser_preserves_exact_prices_and_names) {
  Names accounts;
  Names symbols;
  CsvTrade parsed;
  CHECK(parse_trade_csv("42, ALPHA-7 , AAPL ,BUY,125,191.4201,987654321", accounts,
                        symbols, parsed) == CsvError::None);
  CHECK(parsed.trade.id == 42);
  CHECK(parsed.account == "ALPHA-7");
  CHECK(parsed.symbol == "AAPL");
  CHECK(parsed.trade.side == Side::Buy);
  CHECK(parsed.trade.quantity == 125);
  CHECK(parsed.trade.price == 1'914'201);
  CHECK(parsed.trade.timestamp_ns == 987654321);
}

TEST(csv_trade_parser_reports_specific_input_errors) {
  Names accounts;
  Names symbols;
  CsvTrade parsed;
  CHECK(parse_trade_csv("1,AAPL", accounts, symbols, parsed) == CsvError::FieldCount);
  CHECK(parse_trade_csv("x,A,AAPL,BUY,1,1.00,1", accounts, symbols, parsed) ==
        CsvError::BadEventId);
  CHECK(parse_trade_csv("1,,AAPL,BUY,1,1.00,1", accounts, symbols, parsed) ==
        CsvError::EmptyAccount);
  CHECK(parse_trade_csv("1,A,,BUY,1,1.00,1", accounts, symbols, parsed) ==
        CsvError::EmptySymbol);
  CHECK(parse_trade_csv("1,A,AAPL,HOLD,1,1.00,1", accounts, symbols, parsed) ==
        CsvError::BadSide);
  CHECK(parse_trade_csv("1,A,AAPL,BUY,x,1.00,1", accounts, symbols, parsed) ==
        CsvError::BadQuantity);
  CHECK(parse_trade_csv("1,A,AAPL,BUY,1,1.00001,1", accounts, symbols, parsed) ==
        CsvError::BadPrice);
  CHECK(parse_trade_csv("1,A,AAPL,BUY,1,1.00,t", accounts, symbols, parsed) ==
        CsvError::BadTimestamp);
}
