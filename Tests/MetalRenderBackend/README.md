# Native Metal backend verification

This executable links the production Metal and software offscreen backends
without the game engine. On macOS it verifies:

- padded RGBA uploads, row order, target clear/preserve and readback;
- all 11 Layer methods against the software backend at six opacity values,
  including clipped rectangles and reversed UVs (maximum byte error: 1);
- mesh blend modes, color modulation and the 127/128 mask threshold;
- target clear and consecutive same-target mesh/deformation draws sharing one render encoder;
- clear ordering across target switches, repeated clears, blits, compute and readback;
- clipped `LayerDrawRect` copying at most its affected pixels when a snapshot is needed;
- attachment self-copy, on-demand screenshot color order and letterboxing;
- destruction and recreation of the Metal view on the same SDL window.

The batching, ordering and submission-cadence tests also run with diagnostic
sampling enabled, and verify that diagnostics add no submissions or synchronous
GPU waits. Workload bucket classification and the one-second sampling limit are
tested on every platform.

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
frame time and memory in the same games/scenes; ordinary KRKR Layer trees
still compose on the CPU, so a backend change alone is not proof of lower
memory use or full Layer acceleration.

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

The capability event explicitly says `perStageTimestamps=not_collected`; hardware
counter support is not inferred. Missing Metal timestamps produce `gpuMS=-1`
and `timingAvailable=0`. Submission/completion times use SDL's monotonic tick
clock; render-frame serials count this backend's `BeginFrame` calls. These fields
allow late GPU completion events to be related to earlier CPU events without
mistaking callback time for GPU execution time. Sampling does not split command
buffers or insert fences, counter barriers, submissions or waits.

`metal.cpuWait` independently logs existing readback, in-flight queue and
`nextDrawable` waits of at least 8 ms, at most once per second per kind. Readback
events include the requested region, dimensions, total CPU wall time and the
existing synchronous GPU wait duration. CPU waits are never reported as GPU
stage timings.
