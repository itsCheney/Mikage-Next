# C1 diagnostics validation (2026-10-07)

Portable tests use the production header and an extracted production C work-profile bridge. They do not establish Apple ClangImporter/MSL/device correctness or paired performance.

- `g++ -std=c++17 -Wall -Wextra -Werror -I Engine/KRKRRuntime/Source/cpp/core/render Tests/MetalLayer/C1ProfileTests.cpp build/native-metal-p2c-c1-baseline/c1-profile-main.cpp -o build/native-metal-p2c-c1-baseline/c1-profile-tests.exe`: PASS, then executable PASS.
- Extract real `MikageKRKRRuntime.mm` work bridge and compile same flags with real C header: PASS; `sizeof(MikageKRKRLayerWorkProfile)=267864` bytes (bounded below 300000).
- `python Tests/MetalLayer/test-layer-diagnostics.py`: 25 tests PASS (set TEMP/TMP to workspace `build/tmp` on this sandbox).
- `python scripts/check-krkr-frame-times.py`: PASS.
- `python scripts/check-krkr-work-profile-swift.py`: unavailable on Windows (requires Apple Swift/Xcode); explicitly pending.

C1 profiles aggregate 32 rows by method/reason/alias only, retain last bounded metadata, and report row overflow explicitly. 65536-byte JSON wire capacity has compile-time worst-case escaped-string/uint64/key bound. Transfer subset metrics observe existing successful Record calls and never increment C0 counters. Raw wait samples are limited to active same-generation ShrinkScope successful reads, preserve true zero waits, cap at 2048, and explicitly count drops. Per-read p50/p95 uses raw nearest-rank samples; frame statistics remain the existing raw frame algorithm. Scope wall/preparation metrics are inclusive CPU wall measurements, not GPU execution timings.

C1 scope nesting, exceptions, disabled instrumentation, session generation reset, capacity exhaustion, maximum-width counters, C accessor NUL/version/count rejection, Swift owned-copy boundary tests and legacy analyzer unknowns are covered. Swift App tests and importer tests must run on Apple.


## Logger transport closure (2026-10-08)

Production `DiagnosticLog.record` previously clipped every value at 1024 UTF-8 bytes, which could corrupt bounded profile JSON and raw samples. Only the `layerWorkProfile` event now preserves its fixed whitelist up to the exact production buffer/sample caps. Other fields/events still use the existing 1024-byte clip; whole-record maxBytes, ordering, field count and queue limits are unchanged. Oversized structured values are omitted whole, with `fieldsValueOmitted` and sorted `fieldsValueOmittedKeys`; valid records carry `structuredFieldLimitsVersion=1`.

The App now exports existing `frameSampleCount`; the analyzer optionally verifies the bounded parsed pair count. It reports value omissions, whole-record truncation and logger record losses explicitly. An unmarked historical structured value exactly at 1024 bytes is marked possible clipping, not asserted corruption; relevant completeness flags become conservative. Historical logs are unchanged.

Added four real logger export tests in `Tests/VNCoreTests/DiagnosticLogTests.swift`: >1024 values survive for every applicable large field, C-cap/UTF-8 boundary behavior and whole-value omission are explicit, ordinary event limits and small-segment record guard persist, and nested JSON/control/Unicode escaping round-trips. Tests are source-ready but UNRUN here: Windows host has no Swift toolchain. Apple CI `swift test --filter DiagnosticLogTests` and App/importer tests remain required. Python analyzer now passes all 25 fixtures. Static cap check matches real C headers; frame cap 86016 and wait cap 43008 cover all 2048 UInt64 maximum-width samples.
