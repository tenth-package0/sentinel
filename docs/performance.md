# Performance methodology

Performance numbers in this repository describe a specific workload on a
specific machine. They are evidence for engineering decisions, not a production
capacity promise.

## Engine benchmark

Run the optimized native benchmark with:

```bash
make benchmark
```

The workload is deterministic and contains five million events spread across
256 accounts and 1,024 symbols. Roughly one percent of arrivals are duplicates
and one percent are late. Five timed runs are performed and the median
throughput is reported.

The benchmark also reports lookup cost as the duplicate-ID set grows. This is
important because a tiny cache-resident test can make an open-addressing table
look unrealistically fast. The scaling table shows when the working set leaves
cache and memory access becomes the dominant cost.

## Latency sampling

Per-call latency includes timer overhead. On the development Mac, the observed
clock resolution is 42 ns, so values below that threshold are not measurable.
The benchmark reports the timer resolution beside p50, p90, p99, p99.9,
p99.99, and maximum values instead of implying false precision.

For a serious Linux measurement:

1. Build in release mode with the production compiler and target flags.
2. Pin the process to one isolated core.
3. Fix CPU frequency and disable turbo if comparing implementations.
4. Warm code and data before sampling.
5. Record the CPU model, compiler version, command, and commit SHA.
6. Compare distributions and repeated runs rather than a single best result.

## ITCH replay benchmark

Generate a deterministic, specification-shaped feed:

```bash
python3 bench/make_itch.py build/synthetic.itch 20000000
./build/itch_replay build/synthetic.itch
```

Real compressed sessions can be streamed without keeping a multi-gigabyte file:

```bash
curl -s "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/01302019.NASDAQ_ITCH50.gz" \
  | ./build/itch_replay -
```

Download-bound throughput must not be reported as decoder throughput. Use the
synthetic local feed to measure decoding and the real feed to validate protocol
coverage and state behavior.

## WebAssembly benchmark

The dashboard benchmark executes the deterministic workload inside the browser.
Browser scheduling, JIT warm-up, timer precision, power settings, and other tabs
all affect it. The result is useful for comparing browser builds on the same
device, but it should remain separate from native throughput claims.
