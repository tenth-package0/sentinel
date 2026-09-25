// Engine microbenchmark.
//
// Trades are generated before timing starts (see workload.hpp), so only
// Engine::process is measured, and memory is preallocated so the timed loop
// never allocates.
//
//   throughput: wall time for the whole stream, median of 5 runs
//   latency:    each call timed individually with steady_clock; the clock's own
//               resolution and overhead are printed so the tail can be read honestly

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "sentinel/engine.hpp"
#include "workload.hpp"

using namespace sentinel;
using Clock = std::chrono::steady_clock;

namespace {

double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

long long ns(Clock::duration d) {
  return static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
}

}  // namespace

int main(int argc, char** argv) {
  const std::size_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5'000'000;
  const std::vector<Trade> trades = bench::trades(count);
  Engine engine(bench::config(count));

  // Warm-up pass: touches every page and trains the branch predictors.
  std::uint64_t flagged = 0;
  for (const Trade& t : trades) flagged += engine.process(t).alerts != 0;

  std::vector<double> runs;
  for (int run = 0; run < 5; ++run) {
    engine.reset();
    flagged = 0;
    const auto start = Clock::now();
    for (const Trade& t : trades) flagged += engine.process(t).alerts != 0;
    runs.push_back(seconds(Clock::now() - start));
  }
  std::sort(runs.begin(), runs.end());
  const double median = runs[runs.size() / 2];
  const double flagged_share = static_cast<double>(flagged) / static_cast<double>(count);

  // Per-call latency.
  engine.reset();
  std::uint64_t sink = 0;
  std::vector<long long> latency(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto start = Clock::now();
    const Decision d = engine.process(trades[i]);
    latency[i] = ns(Clock::now() - start);
    sink += d.alerts;
  }
  std::sort(latency.begin(), latency.end());
  const auto pct = [&](double p) { return latency[static_cast<std::size_t>(p * (count - 1))]; };

  // How much of each sample is the clock itself.
  std::vector<long long> empty(100'000);
  for (auto& sample : empty) {
    const auto start = Clock::now();
    sample = ns(Clock::now() - start);
  }
  std::sort(empty.begin(), empty.end());
  long long resolution = 0;
  for (int i = 0; i < 1000 && resolution == 0; ++i) {
    const auto a = Clock::now();
    auto b = Clock::now();
    while (b == a) b = Clock::now();
    resolution = ns(b - a);
  }

  std::printf("Sentinel engine benchmark\n");
  std::printf("  events            %zu (%u accounts x %u symbols, ~1%% duplicates, ~1%% late)\n",
              count, bench::kAccounts, bench::kSymbols);
  std::printf("  flagged           %.1f%% of events raised at least one alert\n",
              100.0 * flagged_share);
  std::printf("\nThroughput (median of %zu runs)\n", runs.size());
  std::printf("  %.1f ns/event   %.1f M events/s\n", median * 1e9 / static_cast<double>(count),
              static_cast<double>(count) / median / 1e6);
  std::printf("\nLatency per call (ns, includes clock overhead)\n");
  std::printf("  p50 %lld   p90 %lld   p99 %lld   p99.9 %lld   p99.99 %lld   max %lld\n", pct(0.5),
              pct(0.9), pct(0.99), pct(0.999), pct(0.9999), latency.back());
  std::printf("  clock: resolution %lld ns, empty-timer p50 %lld ns\n", resolution,
              empty[empty.size() / 2]);
  return sink == 0 ? 1 : 0;  // Uses the results so the compiler cannot skip the work.
}
