# Metal rendering

The application offers two explicit rendering choices:

| Setting | Runtime argument | Work performed |
| --- | --- | --- |
| Metal (default) | `-render=metal` | Native Metal presentation, offscreen targets, Emote meshes and D3DLayer composition |
| 软件合成 · Metal | `-render=software-metal` | Existing software composition with an explicitly selected SDL Metal presenter |

The runtime's OpenGL ES backend still exists and can be reported after a
fallback, but it is no longer selectable: saved `OpenGL ES` and `Metal 原生`
preferences both migrate to native Metal. A native
Metal initialization failure tears down its view and recreates the window for
software rendering with SDL Metal. If that presenter is unavailable, SDL's
platform default is attempted. Logs and the performance HUD report the actual
backend (`metal` or `software/<driver>`), rather than the requested preference.

`MetalRenderBackend` implements the existing compositor interface in
Objective-C++17. The SDL Metal view, command queue and pipeline objects are
owned by the backend and released before the SDL window. The implementation
uses private RGBA8 textures and GPU target aliases, with no permanent CPU
pixel mirror. Upload staging is retained by submitted command buffers and is
bounded by a 16 MiB submission budget and three submissions in flight (a single
large upload can exceed the budget). CPU target readback is allocated only
for `LockTarget` and freed at `UnlockTarget`.

Layer methods use GPU compute with software byte formulas, including signed
shift rounding and alpha preservation. `LayerDrawRect` reads and writes its
destination pixel in place on Tier-2 read/write texture devices when its source
is separate. Other devices snapshot only the affected rectangle; source/target
aliases retain a full source snapshot to preserve sampling semantics.
Consecutive Emote mesh draws to the same target share a render encoder until a
clear, target change, blit, compute operation or submission requires a boundary.
Mesh blending follows the existing OpenGL Emote GPU path, which has its own
blend conventions. Ordinary Emote-to-Layer full overwrites use a GPU copy when
the Layer renderer exposes an unpinned GPU target; incompatible or CPU-backed
Layers retain the readback path. Image and video decoding are unchanged.

The optional `CaptureFrame` backend method and C frame capture/free functions
provide top-down straight-alpha RGBA8 screenshots. Metal replays the current
window composition into a temporary target only when a screenshot is requested;
Swift adds the native menu, HUD and floating controls. The capture requires a
running foreground session on the main thread.

`USE_RENDER_METAL` defaults to ON for Apple SDL3 full builds and OFF elsewhere.
Device and simulator framework build scripts enable it explicitly. An explicit
OFF keeps the existing backends available. ARC applies only to the new Metal
source. See [native tests](../Tests/MetalRenderBackend/README.md) for verification
and the device acceptance checklist.

Changes span the root application, the `krkrsdl3_build` submodule and its nested
`krkrsdl3` source submodule. Before distributing a clean-checkout build, the
nested source changes must be committed/published first, then the build fork's
source pointer and changes, then the application pointer. Local uncommitted
changes do not travel through a root repository commit or recursive CI checkout.

## Frame pacing and diagnostics

The player requests a 60 Hz CADisplayLink range and opts into the iPhone frame
rate hint through its Info.plist. This is a system preference: thermal/power
policy or an overloaded main thread can still reduce the actual callback rate.
Three presentation surfaces are available independently of the two-submission
GPU limit. Drawable size is changed only when the window pixel size changes.

The HUD distinguishes average frame interval from main-thread step wall time,
its one-second peak, the last completed GPU submission duration and the last
frame's queue-capacity/drawable wait. GPU timing excludes queue wait; the wait
metric is already part of main-thread step time. These values must not be added
as independent sequential stages. GPU metrics are asynchronous and are not
necessarily from the same frame as the one-second main-thread average.

Heartbeat logs include the same timings, the requested rate, thermal state and
Low Power Mode. Startup/shader compilation and background time are excluded
from gameplay cadence windows. The FPS metric counts runtime frame submissions,
not script-level content changes. The optional timing methods return -1 on
backends which cannot report the requested GPU/wait measurements.

`heartbeat.profile.commandIntervalProfile` reports counts since the previous
heartbeat, with `ticks` as its denominator for per-step averages. It includes
render/compute/blit encoder counts, mesh and GPU-deformation draws, Emote mask
clears/draws/unique source groups, `LayerDrawRect` snapshot bytes, transient
uploads, command buffers, and ordinary Emote Layer GPU copies versus timed CPU
readbacks. The first heartbeat establishes the counter baseline. A mask group
identity counts source nodes, not unchanged pixels, and is not a cache key.

TJS cleanup also drains inactive register references while globals are live,
and keeps pooling disabled for shutdown-time finalizers. Per-VM GC now actually
compacts that pool. See the [lifecycle regressions](../Tests/KRKRRuntime/README.md).
