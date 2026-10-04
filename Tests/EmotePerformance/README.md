# Emote performance regression tests

These portable C++17 tests compile production cache, bounds and alpha-tile
headers. The node suite compiles the entire production runner, animation and
geometry code; only PSB fixtures, SDL hints and GPU encoding are test hosts.
The production player/adaptor and input-manager translation units are compiled as object targets
to verify complete integration types and dependencies.

```sh
cmake -S Tests/EmotePerformance -B build/emote-performance-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/emote-performance-tests
ctest --test-dir build/emote-performance-tests --output-on-failure
```

Supply `-DEMOTE_GLM_INCLUDE_DIR=/path/to/glm` for an offline build. Otherwise the
node suite downloads GLM 1.0.1 from upstream with a pinned SHA-256.

| Suite | Coverage |
| --- | --- |
| Capture | Ordered shared-player recipes, independent Layer histories, unchanged-content reuse, old/new damage union, moving/hiding/reordering, unknown writes, texture replacement, resize, failed-copy retry, bounded LRU/recipe overflow. A 200-frame pixel oracle checks retained pixels across two destination Layers. |
| Bounds | Submitted CPU vertices, outward pixel rounding, invalid indices/data, unknown GPU fallback. Restricted Bernstein intervals are compared with long-double samples across nested deformation chains, including parent UV extrapolation and negative dimensions. |
| Node | Production local-pose/world-geometry cache dependencies, active submotions, animation clocks, rendering order and caching-on/off equality. |
| Alpha tiles | Production asynchronous cache bytes, publication/display-frame consistency, lifecycle, request budget and retained blocks. The separate LayerInput suite validates production preflight, event FIFO and native mask-hit entry points. |
| Host input | Extracted production SDL admission policy retains all pointer events under alpha backpressure while allowing lifecycle/device/keyboard events through. |

The tests establish CPU policy and mathematical bounds correctness. They do not
measure Metal execution, heat or energy consumption. In particular, a successful
experimental deformation bound remains diagnostic: production cropping still
falls back to the full target for every GPU-deformed drawable. Native Metal
vertex validation and matched-device A/B runs are required before enabling it.

`MIKAGE_EMOTE_CAPTURE_CACHE`, `MIKAGE_EMOTE_LOCAL_UPDATE`,
`MIKAGE_EMOTE_REGION_COPY`, and `MIKAGE_EMOTE_EXPERIMENTAL_BOUNDS` are independent,
default-off experiments. `captureBoundsNS` records production CPU-bound cost;
the experimental-bound counters report candidate area and time separately.
`captureUpdatePixels/captureUpdateFullPixels` measure Layer update coverage;
copy-pixel counters measure transfer coverage. Full GPU overwrite after COW may
still notify a smaller proven update region. CPU readback fallback notifies the
full Layer. Shared canvas draw calls remain encoded in script order; only a
private, self-cleared single-player target may retain identical GPU content.

Local resumed verification: GNU C++ 13.2, Release, five CTest targets passed;
Capture 573 checks, Bounds 23,660 checks, production node/geometry 77 checks,
alpha-tile cache 1,039 checks and host input admission 388 checks. Native
framework/Metal validation is separate. See `docs/EMOTE-PERFORMANCE-STATUS.md`
for opt-in flags, synthetic A/B results and device acceptance requirements.
