// Replays a NASDAQ TotalView-ITCH 5.0 file through the engine.
//
//   ./build/itch_replay 01302019.NASDAQ_ITCH50.gz
//   curl -s https://.../01302019.NASDAQ_ITCH50.gz | ./build/itch_replay -
//
// Two threads connected by a lock-free ring:
//   reader: gunzip -> split messages -> decode executions -> push Trade
//   engine: pop Trade -> Engine::process
// The engine thread owns all engine state, so nothing is locked.

#include <zlib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

#include "sentinel/engine.hpp"
#include "sentinel/itch.hpp"
#include "sentinel/spsc_ring.hpp"

using namespace sentinel;

namespace {

using Ring = SpscRing<Trade, 1 << 16>;

struct Totals {
  std::uint64_t messages = 0;
  std::uint64_t trades = 0;
};

// Reads the file in large chunks and feeds whole messages to the decoder.
bool read_feed(const char* path, itch::Decoder& decoder, Ring& ring, Totals& totals) {
  // "-" reads standard input, so a day can be streamed without saving it.
  gzFile file = std::strcmp(path, "-") == 0 ? gzdopen(0, "rb") : gzopen(path, "rb");
  if (!file) return false;
  gzbuffer(file, 1 << 20);

  std::vector<std::uint8_t> buffer(8 << 20);
  std::size_t filled = 0;
  for (;;) {
    const int n = gzread(file, buffer.data() + filled, static_cast<unsigned>(buffer.size() - filled));
    if (n <= 0) break;
    filled += static_cast<std::size_t>(n);
    const std::size_t used = itch::for_each_message(
        buffer.data(), filled, [&](const std::uint8_t* message, std::size_t length) {
          ++totals.messages;
          if (const auto trade = decoder.decode(message, length)) {
            ++totals.trades;
            while (!ring.try_push(*trade)) std::this_thread::yield();
          }
        });
    std::memmove(buffer.data(), buffer.data() + used, filled - used);
    filled -= used;
  }
  gzclose(file);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <file.NASDAQ_ITCH50[.gz]>\n", argv[0]);
    return 2;
  }

  Config config;
  config.max_accounts = 512;           // Market participant IDs (MPIDs).
  config.max_symbols = 16'384;         // Stock locate codes; a full day uses ~12,000.
  config.position_limit = 1'000'000;   // Shares; ANON aggregates the whole market.
  config.notional_limit = 10'000'000 * kPriceScale;
  config.expected_events = 16'000'000;
  config.keep_log = false;             // The ITCH file is the replayable log.
  auto engine = std::make_unique<Engine>(config);
  auto ring = std::make_unique<Ring>();
  itch::Decoder decoder;
  Totals totals;
  std::atomic<bool> done{false};
  bool opened = false;

  const auto start = std::chrono::steady_clock::now();
  std::thread reader([&] {
    opened = read_feed(argv[1], decoder, *ring, totals);
    done.store(true, std::memory_order_release);
  });

  std::uint64_t processed = 0, duplicates = 0, rejected = 0, flagged = 0;
  std::uint64_t by_alert[std::size(kAllAlerts)] = {};
  for (;;) {
    Trade trade;
    if (!ring->try_pop(trade)) {
      if (done.load(std::memory_order_acquire) && !ring->try_pop(trade)) break;
      if (!done.load(std::memory_order_relaxed)) std::this_thread::yield();
      continue;
    }
    const Decision d = engine->process(trade);
    ++processed;
    duplicates += d.status == Status::Duplicate;
    rejected += !d.accepted() && d.status != Status::Duplicate;
    flagged += d.alerts != 0;
    for (std::size_t i = 0; i < std::size(kAllAlerts); ++i) by_alert[i] += d.has(kAllAlerts[i]);
  }
  reader.join();
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  if (!opened) {
    std::fprintf(stderr, "could not open %s\n", argv[1]);
    return 1;
  }
  std::printf("ITCH replay: %s\n", argv[1]);
  std::printf("  messages     %llu (%.1f M/s)\n", static_cast<unsigned long long>(totals.messages),
              static_cast<double>(totals.messages) / elapsed / 1e6);
  std::printf("  executions   %llu\n", static_cast<unsigned long long>(processed));
  std::printf("  duplicates   %llu   rejected %llu\n", static_cast<unsigned long long>(duplicates),
              static_cast<unsigned long long>(rejected));
  std::printf("  flagged      %llu\n", static_cast<unsigned long long>(flagged));
  for (std::size_t i = 0; i < std::size(kAllAlerts); ++i) {
    std::printf("    %-22s %llu\n", to_string(kAllAlerts[i]), static_cast<unsigned long long>(by_alert[i]));
  }
  std::printf("  participants %zu   wall time %.2f s\n", decoder.accounts().size(), elapsed);
  return 0;
}
