# Native Metal backend verification

This executable links the production Metal and software offscreen backends
without the game engine. On macOS it verifies:

- padded RGBA uploads, row order, target clear/preserve and readback;
- all 11 compositor LayerDrawRect methods against the software backend at six opacity values,
  including clipped rectangles and reversed UVs (maximum byte error: 1);
- mesh blend modes, color modulation and the 127/128 mask threshold;
- target clear and consecutive same-target mesh/deformation draws sharing one render encoder;
- clear ordering across target switches, repeated clears, blits, compute and readback;
- clipped `LayerDrawRect` copying at most its affected pixels when a snapshot is needed;
- attachment self-copy, on-demand screenshot color order and letterboxing;
- destruction and recreation of the Metal view on the same SDL window.

The batching, ordering and submission-cadence tests also run with diagnostic
sampling enabled, and verify that diagnostics add no submissions or synchronous
GPU waits. Workload bucket classification, the one-second sampling limit and
stage timestamp planning/resolution are tested on every platform. Stage tests
cover CPU/GPU clock calibration, overlapping intervals, missing/invalid results,
zero-duration measurements, bounded reservations and encoder-failure rollback.

Install SDL3 and run on a Mac with full Xcode:

```sh
brew install cmake sdl3
cmake -S Tests/MetalRenderBackend -B build/metal-render-tests \
  -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build/metal-render-tests --parallel
MTL_DEBUG_LAYER=1 ctest --test-dir build/metal-render-tests --output-on-failure
```

On Apple hosts without a Metal device, the executable returns 77, which CTest
reports as skipped. A skip does **not** validate native shaders or rendering.
On non-Apple hosts only the software transfer/reference and diagnostic metadata
tests are compiled.
The iOS build workflow compiles this test executable and runs it when the
runner exposes a Metal device.

The framework integration additionally requires device and simulator builds
using `scripts/build-krkr-ios.sh`, plus manual iPhone/iPad checks: all three
settings, fallback logs/HUD, screenshots with menu/floating controls, Retina
dimensions, foreground/background and repeated game switches. Compare CPU,
frame time and memory in the same games/scenes. Ordinary KRKR Layers already use
Metal within their supported method, format, geometry and resource domains;
unsupported calls retain software fallback. See ../MetalLayer for actual software
RenderManager parity and capability audits. Backend tests alone do not establish
complete Layer acceleration or lower memory use.

The Layer suite also covers the P2A prepared affine blend domain. Nonrectangular
Alpha/ConstAlpha/AdditiveAlpha and Photoshop quads use compute with GPU snapshots;
ordinary rectangular shortcuts keep their tile/compute routing. These tests remain
distinct from the compositor's general mesh tests and do not establish perspective
or arbitrary ordinary Layer triangle support.

When the host enables the `MIKAGE_METAL_DIAGNOSTICS` SDL hint, the backend samples
at most one command buffer per second. `metal.gpuCommandBuffer` reports the
completed buffer's real Metal `GPUStartTime`/`GPUEndTime` duration together with
render/compute/blit encoder counts, mesh/deformation/masked draw counts, Layer
dispatches and window draws. `stageMask` bits are mesh/clear=1, Layer compute=2,
blit=4, window rendering=8, other rendering (including capture and texture
initialization)=16. Multiple bits always produce `bucket=mixed`; its GPU duration
cannot be divided among those stages. `maskedDraws` counts draws that sample a
mask, not the source-node draws that generated one. `window_render` measures the
GPU window pass, not display scanout or presentation latency.

The capability event reports `perStageTimestamps=stage_boundary` only when the
OS/device supports stage-boundary sampling, the timestamp counter set exists and
counter storage was allocated. Otherwise it names the unavailable reason. Each
sampled command buffer also emits `metal.gpuStages` with the same `id`. It reports
mesh/window/other vertex and fragment intervals, Layer compute intervals and blit
intervals. Stages can overlap and include scheduling gaps: their durations must
not be added or normalized into a command-buffer budget. Mesh vertex timing is
not a measurement of Bezier evaluation alone; a reused encoder measures its
whole pass, including any clear and multiple draws.

Timestamp samples are limited to 512 per sampled command buffer. An existing
render pass reserves four samples; a compute/blit encoder reserves two. When
full, remaining passes run unsampled rather than splitting or submitting work.
A failed sampled encoder factory rolls back its reservation and retries the
original unsampled factory. Compute passes remain serial. The completion handler
retains only its diagnostic state/device/counter buffer, not the backend.

`metal.gpuStages` includes `status`, `timingAvailable`, calibration validity,
valid/invalid interval counts, dropped/failed pass counts, and per-stage duration
and valid/invalid counts. Missing or entirely invalid stages report `-1`, whereas
a measured zero-length interval reports `0` with a valid count. Partial stage
sums include only valid intervals. Unsupported hardware, allocation failure,
GPU failure, calibration failure and counter resolution failure are explicit.
CPU/GPU clock pairs before encoding and after completion calibrate timestamps;
shared counter storage is resolved on the CPU only after GPU completion.

Missing command-buffer timestamps still produce `gpuMS=-1` and
`timingAvailable=0` in `metal.gpuCommandBuffer`. Submission/completion times use
SDL's monotonic tick clock; render-frame serials count this backend's `BeginFrame`
calls. These fields relate late completion events to earlier CPU events without
mistaking callback time for GPU execution time. Sampling does not split command
buffers or insert fences, counter barriers, submissions or waits.

`metal.cpuWait` independently logs existing readback, in-flight queue and
`nextDrawable` waits of at least 8 ms, at most once per second per kind. Readback
events include the requested region, dimensions, total CPU wall time and the
existing synchronous GPU wait duration. CPU waits are never reported as GPU
stage timings.

Slow readbacks originating in a scoped Layer point query also include
`pointQueryID`, matching the caller's `metal.pointRead` detail record. Only
readbacks admitted by the existing slow-wait threshold and rate limit mark the
caller query as reported; fast/suppressed reads do not trigger caller-stack
capture. Unscoped readbacks and other wait kinds use `pointQueryID=0`.
