# Contributing

Sentinel favors small changes with measurable behavior. A pull request should
make one claim that a reviewer can verify from its tests, benchmark, or output.

## Development setup

Requirements:

- A C++20 compiler
- Make or CMake
- zlib development headers
- Node.js 22 or newer for the dashboard
- Emscripten when changing the WebAssembly boundary

Run the native checks before opening a pull request:

```bash
make all
make test
make sanitize
```

Run `make tsan` on a supported Linux Clang toolchain after changing concurrent
code. Changes to the decoder should also run `make fuzz`.

For browser changes:

```bash
source /path/to/emsdk/emsdk_env.sh
make wasm
cd web
npm ci
npm run build
```

## Design rules

- Keep strings, serialization, and storage outside the engine hot path.
- Use fixed-point integers for money.
- Preserve deterministic results for a fixed event sequence and policy.
- Bound external input before arithmetic or table access.
- Add a differential or invariant test for optimized data structures.
- Document the workload and machine beside any performance claim.

## Commit scope

Write commits that can be reviewed independently. Explain the resulting
behavior in the subject line. Avoid formatting-only churn mixed with logic,
generated benchmark numbers without reproduction commands, and comments that
repeat the code.

## Pull requests

Describe:

1. The behavior or failure mode being changed.
2. Why the chosen design belongs at that layer.
3. The commands used to validate it.
4. Any effect on determinism, memory, latency, or replay compatibility.
