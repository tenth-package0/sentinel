#include <iostream>
#include <string>

#include "sentinel/audit.hpp"
#include "sentinel/csv.hpp"
#include "sentinel/engine.hpp"
#include "sentinel/names.hpp"

using namespace sentinel;

int main() {
  Config config;
  config.max_accounts = 1'024;
  config.max_symbols = 4'096;
  config.expected_events = 1'000'000;
  config.keep_log = false;
  Engine engine(config);
  Names accounts;
  Names symbols;

  std::string line;
  std::size_t line_number = 0;
  std::size_t errors = 0;
  while (std::getline(std::cin, line)) {
    ++line_number;
    if (line.empty() || line.front() == '#') continue;
    if (line_number == 1 && line.starts_with("event_id,")) continue;

    CsvTrade parsed;
    const CsvError error = parse_trade_csv(line, accounts, symbols, parsed);
    if (error != CsvError::None) {
      std::cerr << "line " << line_number << ": " << to_string(error) << '\n';
      ++errors;
      continue;
    }

    const Decision decision = engine.process(parsed.trade);
    std::cout << audit_json({parsed.trade, decision, parsed.account, parsed.symbol}) << '\n';
  }
  return errors == 0 ? 0 : 1;
}
