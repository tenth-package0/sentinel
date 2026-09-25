#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>

#include <emscripten/emscripten.h>

#include "sentinel/engine.hpp"

namespace {

std::unique_ptr<sentinel::SurveillanceEngine> engine;
std::string response_buffer;

std::string escape_json(const std::string& value) {
  std::ostringstream escaped;
  for (const char character : value) {
    switch (character) {
      case '"': escaped << "\\\""; break;
      case '\\': escaped << "\\\\"; break;
      case '\n': escaped << "\\n"; break;
      case '\r': escaped << "\\r"; break;
      case '\t': escaped << "\\t"; break;
      default: escaped << character;
    }
  }
  return escaped.str();
}

void configure_engine(std::int64_t position_limit, double notional_limit) {
  engine = std::make_unique<sentinel::SurveillanceEngine>();
  engine->add_rule(
      std::make_unique<sentinel::PositionLimitRule>(position_limit));
  engine->add_rule(std::make_unique<sentinel::RestrictedSymbolRule>(
      std::unordered_set<std::string>{"GME", "LOCK", "RESTRICTED"}));
  engine->add_rule(
      std::make_unique<sentinel::LargeNotionalRule>(notional_limit));
}

sentinel::SurveillanceEngine& active_engine() {
  if (!engine) configure_engine(1'000, 250'000.0);
  return *engine;
}

const char* store(std::string value) {
  response_buffer = std::move(value);
  return response_buffer.c_str();
}

std::string result_json(const sentinel::ProcessingResult& result) {
  std::ostringstream json;
  json << "{\"eventId\":\"" << escape_json(result.event_id)
       << "\",\"duplicate\":" << (result.duplicate ? "true" : "false")
       << ",\"positionAfter\":" << result.position_after
       << ",\"processingTimeNs\":" << result.processing_time_ns
       << ",\"alerts\":[";
  for (std::size_t i = 0; i < result.alerts.size(); ++i) {
    const auto& alert = result.alerts[i];
    if (i > 0) json << ',';
    json << "{\"code\":\"" << escape_json(alert.code)
         << "\",\"severity\":\"" << sentinel::to_string(alert.severity)
         << "\",\"message\":\"" << escape_json(alert.message) << "\"}";
  }
  json << "]}";
  return json.str();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void sentinel_configure(double position_limit, double notional_limit) {
  configure_engine(static_cast<std::int64_t>(position_limit), notional_limit);
}

EMSCRIPTEN_KEEPALIVE
const char* sentinel_process(const char* event_id, const char* account_id,
                             const char* symbol, int side, double quantity,
                             double price, double event_time_ms,
                             double source_sequence) {
  try {
    const sentinel::Trade trade{
        event_id,
        account_id,
        symbol,
        side == 0 ? sentinel::Side::Buy : sentinel::Side::Sell,
        static_cast<std::int64_t>(quantity),
        price,
        static_cast<std::int64_t>(event_time_ms),
        static_cast<std::uint64_t>(source_sequence),
    };
    return store(result_json(active_engine().process(trade)));
  } catch (const std::exception& error) {
    return store("{\"error\":\"" + escape_json(error.what()) + "\"}");
  }
}

EMSCRIPTEN_KEEPALIVE
const char* sentinel_positions() {
  std::ostringstream json;
  json << '[';
  const auto positions = active_engine().positions();
  for (std::size_t i = 0; i < positions.size(); ++i) {
    if (i > 0) json << ',';
    const auto& position = positions[i];
    json << "{\"accountId\":\"" << escape_json(position.account_id)
         << "\",\"symbol\":\"" << escape_json(position.symbol)
         << "\",\"quantity\":" << position.quantity << '}';
  }
  json << ']';
  return store(json.str());
}

EMSCRIPTEN_KEEPALIVE
const char* sentinel_replay() {
  const auto results = active_engine().replay();
  std::ostringstream json;
  json << "{\"replayed\":" << results.size()
       << ",\"processed\":" << active_engine().processed_event_count() << '}';
  return store(json.str());
}

EMSCRIPTEN_KEEPALIVE
void sentinel_reset() { active_engine().reset(); }

EMSCRIPTEN_KEEPALIVE
double sentinel_processed_count() {
  return static_cast<double>(active_engine().processed_event_count());
}

}  // extern "C"
