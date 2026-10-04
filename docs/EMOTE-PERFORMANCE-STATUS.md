# Emote performance implementation and validation

Implementation branch: `codex/emote-perf` in the app, runtime build and cpp core.
Base commits: app `24c0d57`, build `a7d7867`, core `386526b`.
Local verification recorded on 2026-10-04.

## What changes

- The production runner indexes nodes, retains sorted draw lists and separately
  caches local pose and world geometry. Parent transforms, selected frames,
  materials, variables, viewport, submotions and backend capabilities invalidate
  dependent results. Animation clocks, loops, events, queues and blinks continue
  at their existing cadence. Node traversal remains; whole-tree prepare is not
  skipped using an unsafe `!active` or unchanged-tick inference.
- Content revisions do not depend on diagnostic logging. Shared canvases keep
  ordered clear/player recipes and separate capture histories for each Layer.
  Only a private self-cleared canvas may retain an identical GPU drawing.
  Target writes, replacement, resize and copy failure prevent capture reuse.
- Proven bounds notify `Update(oldBounds union newBounds)`. Region copies retain
  outside pixels; shared/COW textures keep the existing full overwrite rather
  than copying an old full texture before a small patch. CPU fallback invalidates
  the full Layer. GPU-deformed drawable bounds remain unknown in production.
- UI alpha reads use demanded 32x32 tiles, from the MainImage source used by a
  normal window-composition frame. GPU completion and actual presentation both
  gate publication. Pending events pin one frame, its coordinate mapping and
  request state, so later frames cannot keep restarting the same click.
- If a frame has more candidates than the read budget, pending events retain
  its immutable texture epochs through Bitmap COW. Later normal frames first
  supplement that original frame's missing reads, rather than repeatedly
  discarding partial results. Completed tiles release storage before new
  demand; FIFO older events cannot be starved by a stream of new input.
- Pointer chains preserve order; removed/replaced owners cancel the chain.
  At 128 queued pointer events, the host leaves further pointer events in SDL's
  queue while servicing lifecycle, keyboard and device events. Rendering and
  deferred-input completion continue. No down/up events are silently discarded.
- Explicit script pixel and `layer.hitTest()` queries retain their immediate
  semantics. Script callbacks do not inherit the built-in UI snapshot query.

The async cache holds at most 32 tiles per texture, three frame entries per tile;
events may pin entries until delivery. Backend in-flight/ready storage is bounded
to 64 requests and 2 MiB of staging. Consumed/canceled/failed requests explicitly
release storage; command-buffer completion alone does not return storage early.
Pending input may also retain old GPU texture epochs until its reads finish;
include resident-memory/COW costs in the device comparison.

## A/B controls

All new options default off, independent of the existing integrated-animation
switch. App controls are in **设置 → Emote 性能实验** and apply at the next launch.
The runtime's next-launch options mask has the following bit order:

| Bit | SDL hint | Behavior |
| --- | --- | --- |
| 0 | `MIKAGE_EMOTE_NODE_CACHE` | World geometry, shape/surface and draw-list reuse |
| 1 | `MIKAGE_EMOTE_CAPTURE_CACHE` | Proven duplicate capture/drawing reuse |
| 2 | `MIKAGE_EMOTE_LOCAL_UPDATE` | Smaller proven Layer damage notification |
| 3 | `MIKAGE_EMOTE_REGION_COPY` | Smaller GPU transfer when destination is independent |
| 4 | `MIKAGE_EMOTE_ASYNC_ALPHA` | Deferred built-in UI alpha hit testing |
| 5 | `MIKAGE_EMOTE_EXPERIMENTAL_BOUNDS` | Candidate GPU bounds area/time diagnostics only |
| 6 | `MIKAGE_EMOTE_LOCAL_POSE_CACHE` | Experimental local evaluation reuse; requires bit 0 |

The App explicitly overrides stale environment/SDL values per launch. With all
options off, expensive exact geometry/submotion/draw-order signatures are not
compared or retained; content revisions conservatively advance instead.

Cold/new-position input normally waits about one display frame. Resize, absent
drawables or GPU backlog can delay it further, as approved; missing alpha never
means transparent. A static tile may reuse its exact bytes with a fresh display
ticket. Idle input does not trigger continuous full-screen readback.

## Local validation and measured limits

Production C++17 tests pass: Capture 573 checks, Bounds 23,660, Node 77,
alpha tiles 1,039 and host input admission 388. The node executable compiles the
entire production runner/geometry/animation; capture/bounds/alpha tests compile
production headers. LayerInput extracts production dispatch, preflight and mask
hit bodies. MetalLayer compiles the production texture manager and extracts both
native Layer/bitmap ROI paths, checking COW, CPU leases and unaffected pixels.

Existing animation, adapter/TJS, trajectory, host-mode, PSB/shared-resource,
shutdown, cache, session-exit and point-trace regressions pass locally. Windows
Metal tests cover the software reference and metadata/device doubles; they are
not native GPU execution. App preference tests and native Metal region/readback
tests are included for the Apple build.

The Release synthetic benchmark uses 160 shapes/player and 1,200 progress calls.
Its baseline is **this implementation with all new flags off**, not the original
parent checkout. It excludes game scripts, icon fill, Metal, power and thermals.

| Scene | Baseline ms | Geometry cache ms | Change | Plus local-pose cache ms |
| --- | ---: | ---: | ---: | ---: |
| Single static | 302.846 | 74.744 | -75.3% | 85.964 |
| Single 25% moving nodes | 301.030 | 138.797 | -53.9% | 145.468 |
| Single all moving | 300.427 | 317.326 | +5.6% | 318.294 |
| Two static | 607.651 | 152.314 | -74.9% | 178.612 |
| Two 25% moving nodes | 615.111 | 285.726 | -53.5% | 307.744 |
| Two all moving | 609.090 | 627.085 | +3.0% | 649.457 |

Geometry caching is useful when sufficient geometry stays unchanged. Full
activity can regress; local-pose comparison is not worthwhile in this fixture
and stays separately disabled. These percentages are not device heat savings.
Most Metal Emote icons use GPU deformation, so production ROI may remain full
in those scenes; candidate mathematical bounds do not establish a safe Metal
floating-point raster bound and are never used for production cropping.

## Device acceptance run

Apple CI builds the framework/App and compiles the native Metal tests. A runner
without a Metal device may skip native execution; check the actual job output.
The configured local Mac connection was unavailable during this work.

On the same iPhone, compare baseline and individual flags before their combined
configuration: one player, two players, fixed-position/frequency clicks, and a
static-player control. Start from the same save and animation state, brightness,
power connection and thermal state. Run three matched 10-minute rounds after
short profiling runs. Use Instruments CPU/GPU/display/thermal traces and Power
Profiler; preserve smooth motion and compare end-to-end click delay, not merely
the absence of readback waits. Enable a sustained-scene optimization only after
at least 5% repeatable CPU/GPU work or energy improvement without a material
regression; built-in UI readback waits should be zero with async alpha enabled.

`emotePerformanceProfile` logs local/world cache hits, capture skips, update/copy
areas, bounds costs and async requests/pending/bytes. `uiSyncReads/uiSyncWaitNS`
count actual built-in UI readback waits when diagnostic tracing is enabled;
script reads are separate. CaptureCanvas and direct-to-Layer copy counters are
now logged separately.
Alpha byte counts are logical RGBA payload bytes, not padded staging allocation;
alpha failures count event retries caused by failed/canceled read/display state.

Summarize matched diagnostic windows without inferring energy savings:

```sh
python scripts/analyze-emote-performance.py candidate.jsonl --baseline baseline.jsonl --start 30 --end 90
```

Window offsets are seconds after `game.begin`; select matching scenes yourself.
Slow point-read logs are rate limited. GPU stage intervals overlap and must not
be added into a fictitious frame budget. Hardware measurements are the remaining
acceptance step; no default performance option is enabled by this report.
