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

Universal-transition software fallbacks also cover clipped RGBA targets with
independent source/rule origins, pinned CPU caches, and unavailable triple-source
operations. A guarded software target detects right/bottom overruns. Empty target
intersections must leave GPU pixels resident without readback, upload, or a CPU
fallback operation. All three variants exercise both switch and full-table blend
paths.

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

Additive-alpha, Photoshop multiply/overlay/hard-light/screen/color-dodge5, add,
and straight-to-premultiplied conversion compile the production MSL scalar
helpers as C++ and compare 1,835,008 exact pixels with tvpgl, including
channel/alpha/opacity boundaries and grayscale/channel-mask checks.
Overlay and hard-light use the production `/255` table formula. Routing tests
cover canonical/HDA aliases, reference-based conversion after COW, native pixel
access without permanent pinning, ROI-only uploads, raw script pointer coexistence,
exception unwind, and diagnostic interval/reset/overflow and C bridge behavior.
Native Metal shader compilation and device rendering still require Apple CI.

The production initial/temporary bitmap and glyph caches are also checked across
game sessions. Standalone initial bitmap access with no existing holder must keep
its texture alive after holder release and GPU session detach. Retained holders
must rebuild caches in the new session, preserve old snapshots, register compact
hooks again and draw smaller glyphs with no CPU fallback/readback.

`TriangleProfileTests.cpp` exercises the production triangle fallback and the
extracted production C diagnostic bridge. It verifies interval resets (including
maxima and histograms), HUD reads leaving samples intact, enabled/disabled
capture, nested source tags and exception unwind, visible clip areas and full-HD
targets, actual readbacks versus CPU cache hits, target/source/reference aliases,
bounded size histograms, NUL-terminated bridge summaries and unchanged fallback
pixels. The existing two-triangle affine Copy subset has a GPU path; general
triangles and perspective retain software execution. Portable timings are
device-double wall times and do not measure native Metal readback performance.

The P0 capability fixture covers all 85 appendix names: 70 software registrations,
46 names with GPU descriptors, 24 registered methods without descriptors and
15 unregistered historical extensions. Read-only registration snapshots preserve
canonical objects and aliases. Repeated audits never set method parameters,
register missing methods, show a message box or render pixels.

`metal-layer-tests --audit-capabilities` writes JSON with registration, descriptor,
static input/format/parameter/geometry/alias/alpha contracts and separately supplied
backend facts. On portable hosts `deviceDouble=true`; `backendAvailable=true`
means that the double bound successfully and does not establish native Metal.
On Apple hosts an unavailable device is reported independently of mapped methods.
Static contracts do not bypass the current ROI, session, CPU lease or pipeline
checks. Software-only gap contracts remain rectangular baseline domains.

The shared operation definition retains wire/shader IDs 0..26. Contract tests
check Count and invalid values, shared MSL definitions, HDA/reference alpha rules
and a separate TestKind=27 extension fixture. The fixture reuses the definition
expansions and diagnostic storage without adding a production renderer. Dynamic
script compilation failures return null with a diagnostic and leave registration
unchanged, including in Release builds; this does not provide dynamic GLSL support.

```sh
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel
ctest --test-dir build/metal-layer-tests --output-on-failure
```
