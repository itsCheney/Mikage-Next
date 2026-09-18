# Metal rendering

The application now offers three explicit rendering choices:

| Setting | Runtime argument | Work performed |
| --- | --- | --- |
| Metal (default) | `-render=metal` | Native Metal presentation, offscreen targets, Emote meshes and D3DLayer composition |
| 软件合成 · Metal | `-render=software-metal` | Existing software composition with an explicitly selected SDL Metal presenter |
| OpenGL ES | `-render=opengl` | Existing KRKR OpenGL ES backend |

Saved `Metal 原生` preferences migrate to the new native Metal option. A native
Metal initialization failure tears down its view and recreates the window for
software rendering with SDL Metal. If that presenter is unavailable, SDL's
platform default is attempted. Logs and the performance HUD report the actual
backend (`metal` or `software/<driver>`), rather than the requested preference.

`MetalRenderBackend` implements the existing compositor interface in
Objective-C++17. The SDL Metal view, command queue and pipeline objects are
owned by the backend and released before the SDL window. The implementation
uses private RGBA8 textures and GPU target aliases, with no permanent CPU
pixel mirror. Upload staging is retained by submitted command buffers and is
bounded by a 16 MiB submission budget and two submissions in flight (a single
large upload can exceed the budget). CPU target readback is allocated only
for `LockTarget` and freed at `UnlockTarget`.

Layer methods use GPU compute with software byte formulas, including signed
shift rounding and alpha preservation. A reusable GPU destination snapshot
avoids sampling an attachment while writing it. This introduces GPU copy
bandwidth and one scratch texture; memory/performance gains require measurement.
Mesh blending follows the existing OpenGL Emote GPU path, which has its own
blend conventions. Ordinary `tjsNativeLayer`/RenderManager processing remains
on the CPU in this first version. Image and video decoding are unchanged.

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
