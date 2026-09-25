#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sentinel/rule.hpp"
#include "sentinel/types.hpp"

namespace sentinel {

class SurveillanceEngine {
 public:
  SurveillanceEngine() = default;

  void add_rule(std::unique_ptr<Rule> rule);
  ProcessingResult process(const Trade& trade);
  std::vector<ProcessingResult> replay();
  void reset();

  std::vector<Position> positions() const;
  std::vector<AuditRecord> audit_log() const;
  std::size_t processed_event_count() const;

 private:
  using SymbolPositions = std::unordered_map<std::string, std::int64_t>;

  ProcessingResult process_unlocked(const Trade& trade, bool retain_event);
  static void validate(const Trade& trade);
  void reset_state_unlocked(bool clear_events);

  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<Rule>> rules_;
  std::unordered_map<std::string, SymbolPositions> positions_;
  std::unordered_map<std::string, std::int64_t> latest_event_time_;
  std::unordered_set<std::string> processed_event_ids_;
  std::vector<Trade> events_;
  std::vector<AuditRecord> audit_log_;
};

}  // namespace sentinel

