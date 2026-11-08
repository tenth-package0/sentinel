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
  bool first_record = true;
  while (std::getline(std::cin, line)) {
    ++line_number;
    if (line_number == 1 && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
    if (line.empty() || line.front() == '#') continue;
    if (first_record && line.starts_with("event_id,")) {
      first_record = false;
      continue;
    }
    first_record = false;

    CsvTrade parsed;
    const CsvError error = parse_trade_csv(line, accounts, symbols, parsed);
    if (error != CsvError::None) {
      std::cerr << "line " << line_number << ": " << to_string(error) << '\n';
      ++errors;
      continue;
    }

    const Decision decision = engine.process(parsed.trade);
    std::cout << audit_json({parsed.trade, decision, parsed.account, parsed.symbol}) << '\n';
    if (!decision.accepted() && decision.status != Status::Duplicate) ++errors;
  }
  return errors == 0 ? 0 : 1;
}
