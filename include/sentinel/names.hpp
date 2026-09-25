#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sentinel {

// Maps names ("ALPHA-7", "AAPL") to dense IDs 0, 1, 2, ... and back.
//
// This runs at the edge of the system, once per new name, so the engine can
// index plain arrays instead of hashing strings on every trade.
class Names {
 public:
  std::uint32_t intern(std::string_view name) {
    const auto [it, inserted] =
        ids_.try_emplace(std::string(name), static_cast<std::uint32_t>(names_.size()));
    if (inserted) names_.push_back(it->first);
    return it->second;
  }

  const std::string& name(std::uint32_t id) const { return names_.at(id); }
  std::size_t size() const { return names_.size(); }

 private:
  std::unordered_map<std::string, std::uint32_t> ids_;
  std::vector<std::string> names_;
};

}  // namespace sentinel
