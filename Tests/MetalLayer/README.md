# Ordinary Layer rendering and overwrite tests

Build with CMake and run CTest. Apple hosts link the production Metal backend;
non-Apple hosts use a synchronous device double for GPU resources and the
production software renderer for the reference operations. Passing the portable
tests does not measure GPU performance or validate native Metal shaders.

Universal transitions cover `UnivTransBlend`, `_d`, and `_a`. The portable
test compiles the production MSL integer helpers as C++ and compares 3,145,728
pixels exactly against tvpgl: all 256 rule values, phase thresholds, vague
0/1/16/64/255/511/512/1024, extreme alpha pairs, black/white/equal/random RGB.
The `_d` switch path uses `TVPNegativeMulTable` for alpha; its full-table path
uses weighted alpha. These formulas deliberately differ at vague 512.

The existing native Metal CI also exercises real triple-source texture bindings,
independent source/rule origins, all target clipping edges, 1x1/odd rectangles,
both target aliases and shared sources, repeated snapshots and resized snapshots
within one command buffer. Resident transitions assert no software fallback,
readback or upload; native tests additionally assert no per-operation submission
or synchronous wait. Invalid rule format/geometry, unsupported input count and
an unavailable triple-source operation retain software fallback (unavailability
is injected into the portable device double).

On Linux these routing/cache tests use the device double; only the scalar shader
math is executed directly. Native shader compilation and GPU results require a
Mac with Metal. Complete acceptance still needs the same game/transition on an
iPhone/iPad, comparing diagnostic rejects, fallback readbacks, uploads, waits
and frame times before/during/after the transition.

`BitmapOverwriteTests.cpp` compiles the real bitmap declaration and extracted
production copy, adoption and copy-on-write methods. Only font initialization
is stubbed. The layer copy wrappers use their production bodies with a small
facade in place of the script/window machinery. Coverage includes:

- exclusive, shared and static GPU images, snapshot isolation and reference counts;
- no old-image Layer copy, CPU upload or readback on a successful GPU overwrite;
- rejected copies retaining the original image and releasing temporary textures;
- pinned CPU pointers and active read/write leases rejecting GPU overwrite;
- actual image dimensions, clipped CPU fallback, padded source rows and preservation
  of pixels outside a partial copy.

The full iOS framework build remains necessary to validate plugin and script
class integration. Test checks remain active in Release builds.

```sh
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel
ctest --test-dir build/metal-layer-tests --output-on-failure
```
