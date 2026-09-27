# Diagnostic script-stack lifetime

This fixture compiles the production TJS VM and extracts the exact diagnostic
tracer-owner state/scopes from the native host. Foundation's main-thread check
and the host callback pointer are supplied by the test; it is not an iOS build.

```sh
cmake -S Tests/TJSStackTrace -B build/tjs-stack-trace-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/tjs-stack-trace-tests --parallel
ctest --test-dir build/tjs-stack-trace-tests --output-on-failure
```

The tests verify Release script/function attribution without TJS debug mode or
object tracking, nested host callbacks, enabling/disabling during script calls,
exception unwinding, shutdown, nested restart, an independent tracer owner,
off-thread calls and failed startup. Bytecode stripped of source maps retains
function attribution but can report a fallback line, so log positions remain
explicitly unverified.

The diagnostic reference is acquired at an outer main-thread host boundary and
retained until session teardown unwinds. Disabling recording stops point-query
collection immediately; the already-acquired VM tracer reference remains until
that session ends to keep entry/exit stack operations balanced.

`scripts/check-krkr-point-read-trace.py` separately tests the lightweight scoped
query metadata across translation units and threads. `Tests/MetalLayer` tests
actual cache invalidation attribution using a simulated backend report, while
native readback timing and the Objective-C host still require Apple validation.
