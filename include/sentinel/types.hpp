#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sentinel {

enum class Side { Buy, Sell };

inline const char* to_string(Side side) {
  return side == Side::Buy ? "BUY" : "SELL";
}

struct Trade {
  std::string event_id;
  std::string account_id;
  std::string symbol;
  Side side{Side::Buy};
  std::int64_t quantity{0};
  double price{0.0};
  std::int64_t event_time_ms{0};
  std::uint64_t source_sequence{0};
};

enum class Severity { Info, Warning, Critical };

inline const char* to_string(Severity severity) {
  switch (severity) {
    case Severity::Info:
      return "INFO";
    case Severity::Warning:
      return "WARNING";
    case Severity::Critical:
      return "CRITICAL";
  }
  return "UNKNOWN";
}

struct Alert {
  std::string code;
  Severity severity{Severity::Warning};
  std::string message;
};

struct ProcessingResult {
  std::string event_id;
  bool duplicate{false};
  std::int64_t position_after{0};
  std::vector<Alert> alerts;
  std::uint64_t processing_time_ns{0};
};

struct AuditRecord {
  Trade trade;
  ProcessingResult result;
};

struct Position {
  std::string account_id;
  std::string symbol;
  std::int64_t quantity{0};
};

}  // namespace sentinel

