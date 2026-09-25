# Changelog

All notable changes are documented here. Sentinel follows semantic versioning
for its public C++ interfaces and browser bridge.

## 0.3.0 - 2026-09-25

### Added

- NASDAQ TotalView-ITCH 5.0 decoding and order-state reconstruction.
- Lock-free SPSC ingestion between the feed reader and engine owner.
- Flat open-addressing tables for orders and event IDs.
- Differential, sanitizer, thread, and fuzz testing in CI.
- Policy-versioned decisions and machine-readable audit records.
- Streaming CSV ingestion with exact fixed-point decimal prices.
- Live WebAssembly dashboard, event inspection, replay, and audit export.

### Changed

- Replaced string-keyed hot-path state with dense numeric IDs and flat arrays.
- Reworked native benchmarks to disclose workload size, cache scaling, timer
  resolution, and percentile latency.

## 0.2.0 - 2026-09-25

- Compiled the C++20 engine to WebAssembly.
- Added the React surveillance dashboard and Vercel deployment.

## 0.1.0 - 2026-09-25

- Introduced deterministic position tracking, idempotency, replay, and the
  initial position, restriction, notional, and ordering controls.
