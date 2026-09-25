# NASDAQ ITCH adapter

The adapter reads TotalView-ITCH 5.0's length-prefixed binary messages and
converts executions into Sentinel trades. It is intentionally separate from
the surveillance engine: protocol state stays in the decoder, while compliance
state stays in `Engine`.

The implementation follows the
[NASDAQ TotalView-ITCH 5.0 specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf).

## Supported messages

| Type | Message | Decoder behavior |
| --- | --- | --- |
| `R` | Stock directory | Maps a stock-locate code to its ticker |
| `A` | Add order | Stores an anonymous resting order |
| `F` | Add attributed order | Stores the order and its four-byte MPID |
| `E` | Order executed | Emits a trade at the resting limit price |
| `C` | Execution with price | Emits a trade at the execution price |
| `X` | Order cancel | Reduces remaining shares |
| `D` | Order delete | Removes the resting order |
| `U` | Order replace | Moves state to the new reference and terms |
| `P` | Non-cross trade | Emits an anonymous hidden-order trade |

Other message types do not affect the reconstructed order state needed by the
surveillance rules and are ignored.

## Execution identity

ITCH match numbers become Sentinel event IDs. A repeated match number therefore
uses the engine's ordinary idempotency path. The resting order supplies side,
symbol, and participant. Anonymous orders share the `ANON` account.

## Framing and compression

NASDAQ files contain a two-byte big-endian length before each message. The
framer consumes complete messages and retains a trailing partial message for
the next read. `itch_replay` reads through zlib, so it accepts both compressed
files and gzip data streamed on standard input.

```bash
./build/itch_replay session.NASDAQ_ITCH50.gz
gzip -c build/synthetic.itch | ./build/itch_replay -
```

The reader and engine run on separate threads connected by a bounded SPSC ring.
Only the engine thread mutates surveillance state.

## Validation strategy

- Unit fixtures cover every state-changing message listed above.
- Message-size checks follow the published specification.
- A differential order-table test compares optimized storage with
  `std::unordered_map`.
- libFuzzer exercises arbitrary framing and message bytes under ASan and UBSan.
- A deterministic synthetic feed provides repeatable large-stream benchmarks.
