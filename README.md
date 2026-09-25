# Sentinel

A C++20 trade-surveillance engine. It tracks every account's position in every
symbol and checks each trade against compliance limits in about **20 ns**. It
replays real NASDAQ ITCH market data, and the same engine runs in the browser
through WebAssembly.

**Live demo:** [sentinel-demo-peach.vercel.app](https://sentinel-demo-peach.vercel.app)

## Results

Same machine (Apple M4, clang 21, `-O3`), same workload of 5 million trades
across 256 accounts × 1,024 symbols with ~1% duplicate deliveries and ~1% late
timestamps; about a quarter of trades raise an alert.

|                    | v1 (strings, maps, mutex) | v2 (this design) |
| ------------------ | ------------------------- | ---------------- |
| Time per trade     | 635 ns                    | **19.9 ns**      |
| Throughput         | 1.6 M trades/s            | **50 M trades/s** |
| p99 / p99.9        | —                         | 125 / 208 ns     |

v1 is commit `0fc5225`, run on the identical pre-generated trades. Per-call
latency includes the clock's 42 ns resolution on macOS, so the median reads as
one tick; throughput is the precise number. Reproduce with `make benchmark`.

## How it works

The whole design follows from one rule: **do the expensive work once, at the
edge, so the hot path is a few array reads and compares.**

1. **Names become integers at the edge.** `"ALPHA-7"` and `"AAPL"` are mapped to
   small IDs once ([`names.hpp`](include/sentinel/names.hpp)). The engine never
   hashes or compares a string. NASDAQ already sends symbols as integer "locate"
   codes.
2. **State is flat arrays.** A position is `positions[account * symbols + symbol]`;
   limits and restricted flags are per-symbol arrays. Everything is preallocated,
   so processing a trade never allocates.
3. **Duplicates are caught with an open-addressing hash set**
   ([`id_set.hpp`](include/sentinel/id_set.hpp)): one flat array, linear probing,
   so a lookup usually touches one cache line.
4. **Money is an integer.** Prices are in 1/10,000 of a dollar (ITCH's own
   scale). Input bounds guarantee `quantity × price` and every position fit in
   64 bits, so there is no floating-point drift and no overflow.
5. **A decision is one byte of flags.** Each check sets a bit. Human-readable
   explanations are built only when something displays them.
6. **One thread owns the state, so there are no locks.** Other threads hand it
   trades through a lock-free single-producer/single-consumer ring
   ([`spsc_ring.hpp`](include/sentinel/spsc_ring.hpp)).
7. **It is deterministic.** The same trades in the same order always produce
   the same decisions, so the engine can rebuild its state by replaying its log.

```mermaid
flowchart LR
    F[NASDAQ ITCH file] --> D[Decoder thread<br/>gunzip, parse, map to IDs]
    D -->|lock-free ring| E[Engine thread<br/>dedupe, check, update]
    B[Browser] --> W[WASM bridge<br/>JSON and names] --> E2[Same engine,<br/>compiled to WebAssembly]
```

### The checks

| Alert                   | Fires when                                          | Severity |
| ----------------------- | --------------------------------------------------- | -------- |
| `POSITION_LIMIT_BREACH` | \|position after the trade\| is above the symbol's limit | Critical |
| `RESTRICTED_SYMBOL`     | the symbol is on the restricted list                | Critical |
| `LARGE_NOTIONAL`        | quantity × price is above the threshold             | Warning  |
| `OUT_OF_ORDER_EVENT`    | the timestamp is older than the account's latest    | Warning  |

Alerts flag a trade for review; they don't block it. A repeated event ID is
reported as `DUPLICATE` and changes nothing. Malformed input (zero ID, unknown
account, non-positive quantity, out-of-range price) is rejected with a specific
status and also changes nothing.

## How it's tested

- **Differential testing.** [`model_tests.cpp`](tests/model_tests.cpp) runs
  1,000,000 random trades, including duplicates, late timestamps and invalid
  input, through both the engine and a deliberately naive model built on
  `std::map` and `std::set`. Every single decision must match. Changing one `<`
  to `<=` in the engine makes this test fail.
- **Unit tests** for each rule, boundary values, rejection without side
  effects, overflow bounds, replay, and the ITCH decoder, message by message.
- **Sanitizers.** The full suite runs under AddressSanitizer,
  UndefinedBehaviorSanitizer, and ThreadSanitizer (which checks the lock-free ring).
- **Fuzzing.** Random bytes go through the ITCH framing, the decoder and the
  engine under libFuzzer ([`itch_fuzz.cpp`](fuzz/itch_fuzz.cpp)).
- **CI** runs all of the above on GCC and Clang, plus the CMake build, the
  WebAssembly build and the dashboard build ([`ci.yml`](.github/workflows/ci.yml)).

## Real market data: NASDAQ ITCH 5.0

[`itch.hpp`](include/sentinel/itch.hpp) decodes NASDAQ's TotalView-ITCH feed.
It keeps the book of live orders so that each execution can be attributed to
the resting order's symbol, side, price and participant, then feeds it to the
engine as a trade.

```bash
# Sample days are published at https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/ (several GB each)
make build/itch_replay
./build/itch_replay 01302019.NASDAQ_ITCH50.gz
```

## Build and run

Needs a C++20 compiler (Clang or GCC) and zlib.

```bash
make            # demo, tests, benchmark, itch_replay
make test       # 23 tests
make sanitize   # AddressSanitizer + UndefinedBehaviorSanitizer
make tsan       # ThreadSanitizer
make benchmark
./build/demo    # walks through every behaviour in six trades
```

CMake works too: `cmake -S . -B out && cmake --build out && ctest --test-dir out`.

### Web dashboard

```bash
source /path/to/emsdk/emsdk_env.sh
make wasm                        # compiles the engine to web/public/wasm
cd web && npm install && npm run dev
```

The dashboard streams simulated order flow through the WebAssembly engine and
charts it live. You can inject trades, change limits and re-evaluate history
under the new limits, open any decision to see the rule inputs, and run the
benchmark workload in the browser.

## Layout

```text
include/sentinel/   types, engine, id_set, names, spsc_ring, itch
src/                engine.cpp, itch.cpp
apps/               demo, itch_replay, wasm_bridge (browser API)
bench/              benchmark and its shared workload
tests/              unit, differential, ITCH, and ring tests
fuzz/               libFuzzer target for the ITCH decoder
web/                React dashboard
```

## Trade-offs and next steps

- **Memory for speed.** Positions take `accounts × symbols × 8` bytes: 2 MB for
  the benchmark, 64 MB for a full ITCH day. Much larger universes would need a
  sparse layout.
- **The duplicate set grows for the whole session.** A production system would
  reset it at each session boundary (`Engine::reset`).
- **Latency on macOS is limited by a 42 ns clock.** On x86 Linux, timing with
  the CPU cycle counter (`rdtsc`) would resolve the median.
- **State lives in memory.** The replay log (or the ITCH file itself) is the
  source of truth; snapshots and persistence would sit outside the engine.
