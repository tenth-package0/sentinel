#include "sentinel/audit.hpp"

#include <string>

#include "sentinel/engine.hpp"

namespace sentinel {
namespace {

std::string json_string(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (const unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c >= 0x20) out.push_back(static_cast<char>(c));
    }
  }
  out.push_back('"');
  return out;
}

std::string csv_field(std::string_view value) {
  std::string out = "\"";
  for (const char c : value) {
    if (c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

const char* side_name(Side side) { return side == Side::Buy ? "BUY" : "SELL"; }

}  // namespace

std::string audit_json(const AuditRecord& record) {
  const Trade& trade = record.trade;
  const Decision& decision = record.decision;
  std::string alerts = "[";
  for (const Alert alert : kAllAlerts) {
    if (!decision.has(alert)) continue;
    if (alerts.size() > 1) alerts.push_back(',');
    alerts += json_string(to_string(alert));
  }
  alerts.push_back(']');

  return "{\"eventId\":" + std::to_string(trade.id) +
         ",\"account\":" + json_string(record.account) +
         ",\"symbol\":" + json_string(record.symbol) +
         ",\"side\":" + json_string(side_name(trade.side)) +
         ",\"quantity\":" + std::to_string(trade.quantity) +
         ",\"price\":" + json_string(format_price(trade.price)) +
         ",\"timestampNs\":" + std::to_string(trade.timestamp_ns) +
         ",\"status\":" + json_string(to_string(decision.status)) +
         ",\"severity\":" + json_string(to_string(decision.severity())) +
         ",\"position\":" + std::to_string(decision.position) +
         ",\"alerts\":" + alerts + '}';
}

std::string audit_csv_header() {
  return "event_id,account,symbol,side,quantity,price,timestamp_ns,status,severity,position,alerts";
}

std::string audit_csv_row(const AuditRecord& record) {
  std::string alerts;
  for (const Alert alert : kAllAlerts) {
    if (!record.decision.has(alert)) continue;
    if (!alerts.empty()) alerts.push_back('|');
    alerts += to_string(alert);
  }
  return std::to_string(record.trade.id) + ',' + csv_field(record.account) + ',' +
         csv_field(record.symbol) + ',' + side_name(record.trade.side) + ',' +
         std::to_string(record.trade.quantity) + ',' + format_price(record.trade.price) + ',' +
         std::to_string(record.trade.timestamp_ns) + ',' + to_string(record.decision.status) + ',' +
         to_string(record.decision.severity()) + ',' + std::to_string(record.decision.position) + ',' +
         csv_field(alerts);
}

}  // namespace sentinel
