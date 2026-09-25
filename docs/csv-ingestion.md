# CSV ingestion

`csv_ingest` is a small native adapter for piping machine-generated trades into
the engine. It reads standard input and writes newline-delimited JSON audit
records to standard output. Parse errors go to standard error, and any parse
error makes the process exit nonzero after it finishes the stream.

```bash
make build/csv_ingest
./build/csv_ingest < examples/trades.csv
```

## Schema

| Field | Type | Meaning |
| --- | --- | --- |
| `event_id` | unsigned integer | Unique event identifier within the session |
| `account` | nonempty string | Account or participant name |
| `symbol` | nonempty string | Instrument name |
| `side` | `BUY` or `SELL` | Position direction |
| `quantity` | integer | Positive share quantity; engine bounds apply |
| `price` | decimal | Dollars with at most four fractional digits |
| `timestamp_ns` | signed integer | Event time in nanoseconds |

The parser intentionally does not support quoted CSV fields. Account and symbol
names therefore cannot contain commas. This narrower format avoids ambiguous
spreadsheet behavior and keeps the ingestion contract easy to reproduce.

## Exact prices

Decimal price text is converted directly into the engine's 1/10,000-dollar
integer scale. It never passes through a floating-point value. For example,
`191.4201` becomes `1,914,201` internal units.

## Delivery semantics

Repeated `event_id` values are emitted with `DUPLICATE` status and do not modify
positions. Every output document includes the policy version used for its
decision, making streams suitable for audit storage or downstream validation.
