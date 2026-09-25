#include "sentinel/audit.hpp"
#include "sentinel/engine.hpp"
#include "test.hpp"

using namespace sentinel;

TEST(audit_records_are_stable_machine_readable_documents) {
  Config config;
  config.max_accounts = 4;
  config.max_symbols = 4;
  config.position_limit = 100;
  Engine engine(config);

  const Trade trade{42, 125, 191 * kPriceScale + 4200, 123456789, 1, 2, Side::Buy};
  const Decision decision = engine.process(trade);
  const AuditRecord record{trade, decision, "desk, \"alpha\"", "AAPL"};

  const std::string json = audit_json(record);
  CHECK(json.find("\"eventId\":42") != std::string::npos);
  CHECK(json.find("\"account\":\"desk, \\\"alpha\\\"\"") != std::string::npos);
  CHECK(json.find("\"POSITION_LIMIT_BREACH\"") != std::string::npos);
  CHECK(json.find("\"price\":\"191.42\"") != std::string::npos);

  CHECK(audit_csv_header().find("event_id,account,symbol") == 0);
  const std::string csv = audit_csv_row(record);
  CHECK(csv.find("\"desk, \"\"alpha\"\"\"") != std::string::npos);
  CHECK(csv.find("POSITION_LIMIT_BREACH") != std::string::npos);
}
