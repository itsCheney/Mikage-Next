# TJS lifecycle regressions

Build the production TJS VM without SDL or the regular-expression dependency:

```sh
cmake -S Tests/KRKRRuntime -B build/tjs-shutdown-tests
cmake --build build/tjs-shutdown-tests --parallel
ctest --test-dir build/tjs-shutdown-tests --output-on-failure
python3 scripts/check-krkr-frame-times.py
```

The regression creates a script object whose `finalize()` resolves a global
marker, calls a native observer and creates a Dictionary. The last reference
is deliberately retained in an inactive production VM register block.
Deleting the VM must run that finalizer while globals, the register allocator
and script services are still alive. It runs 25 shutdown cases and 25 normal
GC-then-shutdown cases, plus an empty VM teardown and multi-block active GC.

Before the fix, this fixture fails with an access violation during shutdown.
The new cleanup drains unused registers and disables further pooling before
releasing globals. It also covers null custom allocator free and preservation
of the current register-block index during active GC compaction.

The frame timing check compiles the production host accumulator with a fake
clock. It verifies that a 60 Hz cadence with 2 ms of main-thread work reports
those as separate values, that a scene-work spike reaches the peak metric,
and that pause/session reset clears the measurement window.

Regular expressions and platform game/window objects are not exercised by this
standalone VM test. Game-specific exit behavior and rendering/pacing still
require the device test build.
