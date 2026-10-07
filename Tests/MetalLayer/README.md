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
pixels. The two-triangle affine and prepared perspective subsets have GPU paths;
general triangles retain software execution. Portable timings are
device-double wall times and do not measure native Metal readback performance.

The P0 capability fixture covers all 85 appendix names: 70 software registrations,
70 names with GPU descriptors, zero registered methods without descriptors and
15 unregistered historical extensions. Read-only registration snapshots preserve
canonical objects and aliases. Repeated audits never set method parameters,
register missing methods, show a message box or render pixels.

`metal-layer-tests --audit-capabilities` writes JSON with registration, descriptor,
static input/format/parameter/geometry/alias/alpha contracts and separately supplied
backend facts. On portable hosts `deviceDouble=true`; `backendAvailable=true`
means that the double bound successfully and does not establish native Metal.
On Apple hosts an unavailable device is reported independently of mapped methods.
Static contracts do not bypass the current ROI, session, CPU lease or pipeline
checks. The 24 original software contracts remain available alongside the new
GPU contracts; P1A/P1B now describe every registered name, with explicit execution domains.

The shared operation definition retains wire/shader IDs 0..26 and appends P1A
IDs 27..36 and P1B IDs 37..47 (Count 48). Contract tests
check Count and invalid values, shared MSL definitions, HDA/reference alpha rules
and a separate TestKind=48 extension fixture. The fixture reuses the definition
expansions and diagnostic storage without adding a production renderer. Dynamic
script compilation failures return null with a diagnostic and leave registration
unchanged, including in Release builds; this does not provide dynamic GLSL support.

P1A adds Sub/Mul/HDA/ColorDodge/Darken/Lighten/Screen, R8 RemoveOpacity,
AdditiveAlphaToAlpha, Gamma/Gamma_a and the two SD variants. Production MSL
helpers compare 21,757,036 exact pixels against initialized software pointers,
including all 256^3 alpha/mask/opacity combinations. Routing tests retain image
residency, ROI preservation, alias order and CPU pointer/lease fallbacks.
R8 masks require equal-size, forward, in-bounds rectangles; scaling or mirroring
raises an engine error before writes because software ResizeRGBA cannot safely
serve as the mask reference.
New blend, single-source SD and reverse-alpha wrappers also require forward
source rectangles; unsupported mirroring is rejected before writes rather than
given a new GPU result or an undefined software fallback. Invalid 32-bit source
or target formats are similarly rejected. Valid resource/pin failures retain
software execution. These boundaries have exact target-preservation tests.

Gamma uses owned 768-byte B/G/R LUT snapshots, preserving software byte-to-table
ordering and default zero LUT behavior. Normal Gamma leaves alpha-zero pixels
unchanged. Gamma_a now clamps its software LUT index to 255: the former index
256 for channels equal to alpha at 4/8/16/32/64/128 was out of bounds. This is an
explicit software compatibility fix, applied identically in MSL and tested with
independent channel-endpoint expectations.

Tests exercise A/A/B/A and retained old snapshots within pending native work,
short-lived caller parameters, same-LUT reuse, missing-LUT rejection, alpha caches,
and the extracted production bitmap Gamma/COW wrappers. Parameter uploads have
separate counts/bytes; image uploadedBytes excludes them. Native tile/compute and
Apple host/Swift integration remain necessary verification; a device-double pass
does not establish actual Metal compilation or throughput.

P1B adds the remaining 11 Photoshop methods, sharing `layerPsP1BPixel` across
ordinary compute and tile. Tests compare 46,137,346 exact pixels against the
actual initialized HDA function pointers and source/destination table axes.
Source alpha and opacity keep their original `/256` order, including independent
Dodge/Dodge5 and Diff/Diff5 counterexamples. SoftLight/Dodge/Burn use the actual
software tables as an immutable 196608-byte bundle at buffer(3); repeated equal
table supply performs no upload. Other kinds retain the previous helpers and
bind a valid non-lookup placeholder.

PS table counts/bytes are separate from image uploads and Gamma parameters.
`PsTables` is an appended GPU rejection reason for safe resource fallback.
Routing tests require resident inputs to avoid image transfers/fallback, check
ROI and uniform-source scale/clip routing, preserve same-pixel alias and shifted
software order, reject mirrors, and retain CPU leases. With all methods now
described, generic fallback fixtures explicitly use pinned targets or shifted
aliases rather than pretending PsAlphaBlend remains unmapped.

P2A extends prepared affine quads to Alpha, ConstAlpha, AdditiveAlpha and all
registered Photoshop families, including their registered alpha/HDA aliases.
The nonrectangular path samples the existing software-compatible warp and applies
the ordinary pixel helper against the old target across the full clip. Transparent
warp borders participate in blending. Reference is ignored, matching the software
triangle implementation, and overlapping warp sources are snapshotted on the GPU.
The axis-aligned shortcut reuses ordinary rectangles; shifted/scaled self-blends
retain scanline software semantics and report `AffineAlias`. Source crops must be
forward, integral and in bounds, clips must already be clamped to the target, and
StretchType is restricted to 0..2. Mirrored axis-aligned scanlines, fractional source
crops, non-affine quads and other filters retain their existing software behavior.

Nonrectangular affine blends currently use compute plus GPU snapshots on every
device. They end any active tile pass; consecutive rectangular shortcuts can still
reuse it. This is a correctness path, with no new per-operation submit or wait.
The portable double executes extracted coordinate/byte helpers and the initialized
software blend on the sampled frame. Native Apple tests use the production kernel;
portable results do not validate MSL compilation, texture binding or GPU ordering.
Existing scalar blend tests remain independent coverage of the shared pixel math.

P2B adds inverse homography compute for Copy and the P2A blend families, including
the shared `PerspectiveAlphaBlend_a` object. LT/RT/LB/RB coordinates preserve the
software right/bottom +1 warp convention, whole-source sampling, clip-relative
pixel centers, transparent borders and byte rounding. Rectangular shortcuts use
point 3 for the lower-right corner; the software index-2 bug and disjoint-rectangle
full-clip warp were fixed before establishing the new reference. Clips are clamped,
all quads are validated before writes, negative filters and singular/nonfinite
mappings raise controlled errors, and unsupported valid domains retain software.

Up to 256 quads execute as one ordered backend transaction. A scratch target and
per-quad snapshots ensure aliased sources see earlier results; the real target is
written only after every quad encodes successfully. Rectangular shifted/scaled
self-aliases (including Copy), StretchType outside 0..2 and inverse coefficients
above 1e6 retain software execution. `PerspectiveAlias` is a distinct rejection
reason. Reference is ignored as in software. This path uses compute and GPU scratch
textures, with no new per-operation submit/wait; it does not use affine triangles.

Tests cover 32 names, exact scalar coordinates, strong perspective/reflections,
source/target clipping, fractional/out-of-range source coordinates, mixed ordered
quads, failed late quads, default unsupported backends, table failures, COW/alpha
caches/CPU leases and pending multi-target batches. Injected mid-batch failures also
read the physical backend target/source to ensure CPU caches cannot hide a partial
commit. The portable double preserves the software filter while using sampled-frame
blend references; native MSL, GPU ordering and scratch-resource cost need Apple and
device validation. Perspective support does not imply arbitrary Layer triangles.

```sh
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel
ctest --test-dir build/metal-layer-tests --output-on-failure
```

P2C C0 adds a parallel direction/origin view to the existing texture top-eight
profile: 64 bounded slots, protected names, explicit capacity/oversize overflow,
and exact calls/bytes/wall/wait totals. Production read/upload/point paths capture
the profiling generation before backend work; cache hits and failed operations do
not count. Tests cover deferred long names, raw lease changes, all four mid-backend
generation changes and identical pixels/backend work with diagnostics on/off.

Runtime frame sampling stores at most 2048 interval/CPU-wall pairs per window,
retains history across sampling, and resets on lifecycle/recording boundaries.
The extracted production C bridge verifies complete bounded strings/arrays and
old-field compatibility. Parser fixtures validate v1 lower bounds, v2 reconciliation,
overflow/missing samples, complete windows and raw-sample nearest-rank quantiles.
App metadata wiring checks run portably; SwiftUI/XCTest and real-device baselines
remain separate Apple acceptance. No C1-C4 rendering optimization is implied.
