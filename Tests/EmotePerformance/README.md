# Emote runner regression tests

Portable C++17 tests that compile the entire production Emote runner,
animation and geometry code; only PSB fixtures, SDL hints and GPU encoding are
test hosts. The production player/adaptor and Layer-manager translation units
are also compiled as object targets, to catch integration and header errors.

```sh
cmake -S Tests/EmotePerformance -B build/emote-performance-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/emote-performance-tests
ctest --test-dir build/emote-performance-tests --output-on-failure
build/emote-performance-tests/emote-node-tests bench   # optional progress() timing
```

Supply `-DEMOTE_GLM_INCLUDE_DIR=/path/to/glm` for an offline build. Otherwise the
node suite downloads GLM 1.0.1 from upstream with a pinned SHA-256.

The node suite covers parent/root/viewport transforms, active sub-motions and
their pooling, animation variables, clone and save/restore, shared-frame
sentinels, CPU and GPU icon meshes, draw order, early seeks, and that the
hit-test coverage query (`mayCoverPixel`) contains every exact mesh hit. It
does not measure Metal execution, heat or energy.

`emote-capture-exchange-tests` drives the production `D3DAdaptor` capture
state machine against a pixel model of the backend: textures are exchanged
only after the script has cleared the canvas after captures, uncleared draws
and repeated captures still see the drawn frame, and each capture's
transparency hint names exactly the frames drawn since the last clear.

The opt-in Emote performance experiments (node/capture caches, local update,
region copy, async pointer alpha, experimental bounds, local pose cache) and
their Capture, Bounds, Alpha-tile and Host-input suites were removed on
2026-10-12.
