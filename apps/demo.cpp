// Walks through each behaviour of the engine with a handful of trades.

#include <cstdio>
#include <string>

#include "sentinel/engine.hpp"
#include "sentinel/names.hpp"

using namespace sentinel;

int main() {
  Names accounts;
  Names symbols;
  Engine engine;
  engine.set_position_limit(symbols.intern("AAPL"), 500);
  engine.restrict_symbol(symbols.intern("LOCK"));

  struct Row {
    EventId id;
    const char* account;
    const char* symbol;
    Side side;
    std::int64_t quantity;
    double price;
    std::int64_t time;
    const char* note;
  };
  const Row rows[] = {
      {1, "alpha", "AAPL", Side::Buy, 300, 190.00, 1'000, "normal trade"},
      {2, "alpha", "AAPL", Side::Buy, 250, 191.00, 1'010, "crosses the AAPL limit of 500"},
      {3, "bravo", "LOCK", Side::Sell, 40, 55.00, 1'020, "restricted symbol"},
      {4, "alpha", "MSFT", Side::Buy, 800, 420.00, 990, "large notional, and older than alpha's last event"},
      {1, "alpha", "AAPL", Side::Buy, 300, 190.00, 1'000, "same event delivered twice"},
      {5, "bravo", "MSFT", Side::Buy, 0, 420.00, 1'030, "invalid quantity"},
  };

  std::printf("Sentinel demo\n\n");
  for (const Row& row : rows) {
    Trade trade;
    trade.id = row.id;
    trade.account = accounts.intern(row.account);
    trade.symbol = symbols.intern(row.symbol);
    trade.side = row.side;
    trade.quantity = row.quantity;
    trade.price = parse_price(row.price);
    trade.timestamp_ns = row.time;

    const Decision d = engine.process(trade);
    std::printf("#%llu %-5s %-4s %4lld @ %-7s -> %-9s position %lld   (%s)\n",
                static_cast<unsigned long long>(row.id), row.account, row.side == Side::Buy ? "BUY" : "SELL",
                static_cast<long long>(row.quantity), format_price(trade.price).c_str(),
                to_string(d.status), static_cast<long long>(d.position), row.note);
    for (const Alert alert : kAllAlerts) {
      if (!d.has(alert)) continue;
      std::printf("      [%s] %s: %s\n", to_string(severity_of(alert)), to_string(alert),
                  explain(alert, trade, d, engine, row.account, row.symbol).c_str());
    }
  }

  std::printf("\nPositions\n");
  for (const Position& p : engine.positions()) {
    std::printf("  %-6s %-5s %lld\n", accounts.name(p.account).c_str(), symbols.name(p.symbol).c_str(),
                static_cast<long long>(p.quantity));
  }
}
