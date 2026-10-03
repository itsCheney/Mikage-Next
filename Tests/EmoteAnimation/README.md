# Emote animation integration tests

The core target compiles the production `emoteanimation.cpp` directly. The
integration target extracts the production engine adapter, time/variable/
timeline methods, snapshots and D3D forwarding/clone bodies at build time,
and compiles them with the real TJS VM and ncbind. Its raw-callback registration
is taken from production. Resource definitions, window/GPU construction and
unrelated player members are fixtures; this is not a full runtime or Metal test.

Requires C++17, CMake 3.20, and Python 3:

```sh
cmake -S Tests/EmoteAnimation -B build/emote-animation-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/emote-animation-tests --parallel
ctest --test-dir build/emote-animation-tests --output-on-failure
```

On Windows, use `-G Ninja` and `-DPython3_EXECUTABLE=C:/path/to/python.exe` if
the configured Python cannot be found. The tests use checked failures rather
than assertions, so Release builds execute their validations.

Coverage includes independent and staggered timelines, intro/loop boundaries,
fractional and large deltas, sparse controller scheduling, easing, queued and
interrupted transitions, weighted differences/fades, instant variables,
snapshot validation/UTF-8 labels, resource identity, real TJS optional argument
dispatch, ordinary/D3D units, explicit pause/seek, main-motion completion,
blink-state snapshots, and the production D3D clone flow. The legacy route is
also exercised.

The host-mode target extracts the actual App host setter/startup hint code and
production Emote constructor. SDL hint storage is a fixture; it verifies the
default/enable/disable session sequence, override priority, no mid-session mode
change and failure propagation. XCTest in `Tests/MikageTests` covers App setting
persistence/migration and launch snapshots on the iOS simulator.

A deterministic synthetic trajectory can be exported for comparison:

```sh
build/emote-animation-tests/emote-animation-tests --trajectory 60 10 > build/emote-60.csv
build/emote-animation-tests/emote-animation-tests --trajectory 42 10 > build/emote-42.csv
```

On Windows append `.exe`. These CSVs use the published sparse `body_UD` test
pattern; they are not a decoded game PSB or an official SDK reference. No
commercial resources are committed. Per-resource node curve/type tests and
real-device cadence/performance measurements remain separate acceptance work.
