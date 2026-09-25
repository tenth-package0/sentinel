// Engine microbenchmark.
//
// Trades are generated before timing starts (see workload.hpp), so only
// Engine::process is measured, and memory is preallocated so the timed loop
// never allocates.
//
//   throughput: wall time for the whole stream, median of 5 runs
//   scaling:    throughput at smaller sizes, to show where the time goes: the
//               duplicate-ID set grows with the stream, and once it outgrows the
//               CPU caches each lookup becomes a trip to main memory
//   latency:    each call timed individually with steady_clock; the clock's own
//               resolution and overhead are printed so the tail can be read honestly

#include <algorithm>
#include <bit>
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

// Median nanoseconds per trade over five timed passes, after a warm-up pass
// that touches every page and trains the branch predictors.
double median_ns(Engine& engine, const std::vector<Trade>& trades, double& flagged_share) {
  std::uint64_t flagged = 0;
  for (const Trade& t : trades) flagged += engine.process(t).alerts != 0;

  std::vector<double> runs;
  for (int run = 0; run < 5; ++run) {
    engine.reset();
    flagged = 0;
    const auto start = Clock::now();
    for (const Trade& t : trades) flagged += engine.process(t).alerts != 0;
    runs.push_back(seconds(Clock::now() - start) * 1e9 / static_cast<double>(trades.size()));
  }
  std::sort(runs.begin(), runs.end());
  flagged_share = static_cast<double>(flagged) / static_cast<double>(trades.size());
  return runs[runs.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
  const std::size_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5'000'000;
  const std::vector<Trade> trades = bench::trades(count);
  Engine engine(bench::config(count));

  double flagged_share = 0;
  const double ns_per_trade = median_ns(engine, trades, flagged_share);

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
  std::printf("\nThroughput (median of 5 runs)\n");
  std::printf("  %.1f ns/event   %.1f M events/s\n", ns_per_trade, 1e3 / ns_per_trade);

  std::printf("\nScaling: time per trade vs. size of the duplicate-ID set\n");
  for (const std::size_t n : {10'000ul, 100'000ul, 1'000'000ul, count}) {
    if (n > count) continue;
    const std::vector<Trade> subset(trades.begin(), trades.begin() + static_cast<long>(n));
    Engine small(bench::config(n));
    double unused = 0;
    const double t = n == count ? ns_per_trade : median_ns(small, subset, unused);
    const double set_mb = static_cast<double>(std::bit_ceil(n * 2) * sizeof(std::uint64_t)) / 1e6;
    std::printf("  %9zu trades   set %7.2f MB   %5.1f ns/event\n", n, set_mb, t);
  }
  std::printf("\nLatency per call (ns, includes clock overhead)\n");
  std::printf("  p50 %lld   p90 %lld   p99 %lld   p99.9 %lld   p99.99 %lld   max %lld\n", pct(0.5),
              pct(0.9), pct(0.99), pct(0.999), pct(0.9999), latency.back());
  std::printf("  clock: resolution %lld ns, empty-timer p50 %lld ns\n", resolution,
              empty[empty.size() / 2]);
  return sink == 0 ? 1 : 0;  // Uses the results so the compiler cannot skip the work.
}
