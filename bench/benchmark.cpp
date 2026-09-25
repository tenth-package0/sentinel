#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "sentinel/engine.hpp"

int main(int argc, char** argv) {
  const std::size_t event_count =
      argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 100'000;

  sentinel::SurveillanceEngine engine;
  engine.add_rule(std::make_unique<sentinel::PositionLimitRule>(1'000'000));
  engine.add_rule(std::make_unique<sentinel::LargeNotionalRule>(10'000'000.0));

  std::vector<std::uint64_t> latencies;
  latencies.reserve(event_count);
  const auto started = std::chrono::steady_clock::now();

  for (std::size_t i = 0; i < event_count; ++i) {
    sentinel::Trade trade{
        "event-" + std::to_string(i),
        "account-" + std::to_string(i % 100),
        i % 2 == 0 ? "AAPL" : "MSFT",
        i % 3 == 0 ? sentinel::Side::Sell : sentinel::Side::Buy,
        static_cast<std::int64_t>((i % 50) + 1),
        100.0 + static_cast<double>(i % 300),
        static_cast<std::int64_t>(1'700'000'000'000ULL + i),
        i,
    };
    latencies.push_back(engine.process(trade).processing_time_ns);
  }

  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  std::sort(latencies.begin(), latencies.end());
  const auto percentile = [&](double p) {
    const auto index = static_cast<std::size_t>(p * (latencies.size() - 1));
    return latencies[index];
  };

  std::cout << "Sentinel benchmark\n"
            << "events: " << event_count << '\n'
            << "elapsed_seconds: " << std::fixed << std::setprecision(4) << elapsed
            << '\n'
            << "throughput_events_per_second: "
            << static_cast<std::uint64_t>(event_count / elapsed) << '\n'
            << "p50_processing_ns: " << percentile(0.50) << '\n'
            << "p95_processing_ns: " << percentile(0.95) << '\n'
            << "p99_processing_ns: " << percentile(0.99) << '\n';
}

