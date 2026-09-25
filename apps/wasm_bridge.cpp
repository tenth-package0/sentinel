// C API for the browser dashboard. Compiled with Emscripten (`make wasm`).
//
// This is the text edge of the system: names arrive as strings, are mapped to
// integer IDs here, and results go back as JSON. The engine itself is the same
// code the native build and benchmark use.

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sentinel/engine.hpp"
#include "sentinel/names.hpp"
#include "../bench/workload.hpp"

using namespace sentinel;

namespace {

constexpr std::uint32_t kCapacity = 256;  // Accounts and symbols the demo supports.


struct State {
  explicit State(const Config& config) : engine(config) {}
  Engine engine;
  Names accounts;
  Names symbols;
  std::unordered_map<std::string, EventId> event_ids;
  std::string response;  // Single-threaded target: one reusable response buffer.
};

std::unique_ptr<State> state;

void configure(double position_limit, double notional_limit) {
  Config config;
  config.max_accounts = kCapacity;
  config.max_symbols = kCapacity;
  config.expected_events = 1 << 16;
  config.position_limit = static_cast<std::int64_t>(position_limit);
  config.notional_limit = parse_price(notional_limit);
  state = std::make_unique<State>(config);
  for (const char* symbol : {"GME", "LOCK", "RESTRICTED"}) {
    state->engine.restrict_symbol(state->symbols.intern(symbol));
  }
}

State& active() {
  if (!state) configure(1'000, 250'000);
  return *state;
}

// Doubles from JavaScript may be NaN or huge; casting those to an integer is
// undefined behaviour, so anything out of range becomes 0 and fails validation.
std::int64_t to_int(double value) {
  return std::isfinite(value) && std::fabs(value) < 9e18 ? static_cast<std::int64_t>(value) : 0;
}

Price to_price(double dollars) {
  return std::isfinite(dollars) && std::fabs(dollars) < 1e12 ? parse_price(dollars) : 0;
}

std::string quoted(const std::string& text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    if (static_cast<unsigned char>(c) >= 0x20) out += c;
  }
  return out + '"';
}

const char* reply(std::string json) {
  active().response = std::move(json);
  return state->response.c_str();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void sentinel_configure(double position_limit, double notional_limit) {
  configure(position_limit, notional_limit);
}

EMSCRIPTEN_KEEPALIVE const char* sentinel_process(const char* event_id, const char* account,
                                                  const char* symbol, int side, double quantity,
                                                  double price, double time_ms) {
  State& s = active();
  const auto [id, inserted] =
      s.event_ids.try_emplace(event_id, s.event_ids.size() + 1);

  Trade trade;
  trade.id = id->second;
  trade.account = s.accounts.intern(account);
  trade.symbol = s.symbols.intern(symbol);
  trade.side = side == 0 ? Side::Buy : Side::Sell;
  trade.quantity = to_int(quantity);
  trade.price = to_price(price);
  trade.timestamp_ns = to_int(time_ms) * 1'000'000;

  const Decision d = s.engine.process(trade);
  if (!d.accepted() && d.status != Status::Duplicate) {
    return reply(std::string("{\"error\":\"") + to_string(d.status) + "\"}");
  }

  std::string json = "{\"eventId\":" + quoted(event_id) +
                     ",\"duplicate\":" + (d.status == Status::Duplicate ? "true" : "false") +
                     ",\"positionAfter\":" + std::to_string(d.position) +
                     ",\"policyVersion\":" + std::to_string(d.policy_version) +
                     ",\"alerts\":[";
  bool first = true;
  if (d.status == Status::Duplicate) {
    json += R"({"code":"DUPLICATE_EVENT","severity":"INFO","message":"Event was already processed"})";
    first = false;
  }
  for (const Alert alert : kAllAlerts) {
    if (!d.has(alert)) continue;
    if (!first) json += ',';
    first = false;
    json += std::string("{\"code\":\"") + to_string(alert) + "\",\"severity\":\"" +
            to_string(severity_of(alert)) + "\",\"message\":" +
            quoted(explain(alert, trade, d, s.engine, account, symbol)) + '}';
  }
  return reply(json + "]}");
}

// Runs the native benchmark's workload (bench/workload.hpp) on a separate,
// preallocated engine, so the dashboard's state is untouched. Returns the
// median nanoseconds per trade over three timed passes.
EMSCRIPTEN_KEEPALIVE double sentinel_benchmark(int count) {
  const auto events = static_cast<std::size_t>(std::max(count, 1));
  const std::vector<Trade> trades = bench::trades(events);
  Engine engine(bench::config(events));
  for (const Trade& t : trades) engine.process(t);  // Warm-up.

  std::vector<double> runs;
  std::uint64_t flagged = 0;
  for (int run = 0; run < 3; ++run) {
    engine.reset();
    const double start = emscripten_get_now();
    for (const Trade& t : trades) flagged += engine.process(t).alerts != 0;
    runs.push_back((emscripten_get_now() - start) * 1e6 / static_cast<double>(events));
  }
  std::sort(runs.begin(), runs.end());
  return flagged == 0 ? 0 : runs[runs.size() / 2];
}

EMSCRIPTEN_KEEPALIVE const char* sentinel_positions() {
  State& s = active();
  std::string json = "[";
  for (const Position& p : s.engine.positions()) {
    if (json.size() > 1) json += ',';
    json += "{\"accountId\":" + quoted(s.accounts.name(p.account)) +
            ",\"symbol\":" + quoted(s.symbols.name(p.symbol)) +
            ",\"quantity\":" + std::to_string(p.quantity) + '}';
  }
  return reply(json + "]");
}

EMSCRIPTEN_KEEPALIVE const char* sentinel_replay() {
  State& s = active();
  const std::size_t replayed = s.engine.replay();
  return reply("{\"replayed\":" + std::to_string(replayed) +
               ",\"processed\":" + std::to_string(s.engine.accepted_count()) + '}');
}

EMSCRIPTEN_KEEPALIVE void sentinel_reset() {
  State& s = active();
  s.engine.reset();
  s.event_ids.clear();
}

}  // extern "C"
