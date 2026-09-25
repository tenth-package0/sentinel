#!/usr/bin/env python3
"""Writes a synthetic NASDAQ ITCH 5.0 file for benchmarking the decoder.

The message mix loosely follows a real day: mostly order adds and deletes,
some cancels, replaces, and executions, with about a million live orders.

    python3 bench/make_itch.py build/synthetic.itch 20000000
"""

import random
import struct
import sys

SYMBOLS = 500
LIVE_TARGET = 1_000_000


def main(path, count):
    rng = random.Random(1)
    live = []  # Order references that can still be executed or deleted.
    ref = match = 1
    ts = 34_200_000_000_000  # 09:30 in ns since midnight.

    with open(path, "wb") as out:
        def write(kind, locate, body):
            message = kind + struct.pack(">HH", locate, 0) + ts.to_bytes(6, "big") + body
            out.write(struct.pack(">H", len(message)) + message)

        for locate in range(1, SYMBOLS + 1):
            write(b"R", locate, f"S{locate}".encode().ljust(8) + b"QN" + struct.pack(">I", 100) + b" " * 19)

        for _ in range(count):
            ts += 50
            roll = rng.random()
            if not live or (roll < 0.47 and len(live) < LIVE_TARGET * 1.2) or len(live) < LIVE_TARGET * 0.8:
                side = b"B" if rng.random() < 0.5 else b"S"
                body = struct.pack(">Q", ref) + side + struct.pack(">I", rng.randint(1, 500)) + b"X".ljust(8)
                body += struct.pack(">I", rng.randint(10, 500) * 10_000)
                if rng.random() < 0.1:
                    write(b"F", rng.randint(1, SYMBOLS), body + rng.choice([b"GSCO", b"MSCO", b"JPMS", b"CDRG"]))
                else:
                    write(b"A", rng.randint(1, SYMBOLS), body)
                live.append(ref)
                ref += 1
                continue

            i = rng.randrange(len(live))
            order = live[i]
            live[i] = live[-1]
            live.pop()
            if roll < 0.85:
                write(b"D", 1, struct.pack(">Q", order))
            elif roll < 0.90:
                write(b"X", 1, struct.pack(">QI", order, 1))
                live.append(order)
            elif roll < 0.93:
                write(b"U", 1, struct.pack(">QQII", order, ref, rng.randint(1, 500), rng.randint(10, 500) * 10_000))
                live.append(ref)
                ref += 1
            else:
                write(b"E", 1, struct.pack(">QIQ", order, rng.randint(1, 500), match))
                match += 1


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 20_000_000)
