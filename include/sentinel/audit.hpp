#pragma once

#include <string>
#include <string_view>

#include "sentinel/types.hpp"

namespace sentinel {

// A presentation-layer record assembled after the engine returns a decision.
// Keeping names here preserves the integer-only engine hot path while giving
// storage and transport adapters a stable, self-contained audit payload.
struct AuditRecord {
  Trade trade;
  Decision decision;
  std::string_view account;
  std::string_view symbol;
};

std::string audit_json(const AuditRecord& record);
std::string audit_csv_header();
std::string audit_csv_row(const AuditRecord& record);

}  // namespace sentinel
