# Native Metal backend verification

This executable links the production Metal and software offscreen backends
without the game engine. On macOS it verifies:

- padded RGBA uploads, row order, target clear/preserve and readback;
- all 11 Layer methods against the software backend at six opacity values,
  including clipped rectangles and reversed UVs (maximum byte error: 1);
- mesh blend modes, color modulation and the 127/128 mask threshold;
- consecutive same-target mesh draws sharing one render encoder;
- clipped `LayerDrawRect` copying at most its affected pixels when a snapshot is needed;
- attachment self-copy, on-demand screenshot color order and letterboxing;
- destruction and recreation of the Metal view on the same SDL window.

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
On non-Apple hosts only the software transfer/reference tests are compiled.
The iOS build workflow compiles this test executable and runs it when the
runner exposes a Metal device.

The framework integration additionally requires device and simulator builds
using `scripts/build-krkr-ios.sh`, plus manual iPhone/iPad checks: all three
settings, fallback logs/HUD, screenshots with menu/floating controls, Retina
dimensions, foreground/background and repeated game switches. Compare CPU,
frame time and memory in the same games/scenes; ordinary KRKR Layer trees
still compose on the CPU, so a backend change alone is not proof of lower
memory use or full Layer acceleration.
