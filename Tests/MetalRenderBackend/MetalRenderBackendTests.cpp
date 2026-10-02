#include "backend/SWRenderBackend.h"
#include "backend/MetalRenderBackend.h"
#include "backend/MetalStageDiagnostics.h"
#include "PointReadTrace.h"
#ifdef TEST_NATIVE_METAL
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

using krkrsdl3::iTVPRenderBackend;

// The independent test executable does not link the engine/session. Only the
// software *offscreen* renderer is used; SDL presenter calls are unexpected.
namespace krkrsdl3 {
void TVPRegisterRenderBackend(const TVPRenderBackendDesc&) {}
void TVPRecordMeshDraw(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {}
void TVPRecordEmoteGPUDeform(uint64_t) {}
// Observable so tests can assert submission cadence: GPU-only work must not
// force command-buffer submits (see SubmissionCadenceTests).
int g_metalSubmits = 0;
int g_renderEncoders = 0;
int g_syncWaits = 0;
uint64_t g_layerRectSnapshotBytes = 0;
void TVPRecordMetalSubmit() { ++g_metalSubmits; }
void TVPRecordMetalRenderEncoder() { ++g_renderEncoders; }
void TVPRecordMetalComputeEncoder() {}
void TVPRecordMetalBlitEncoder() {}
void TVPRecordMetalLayerRectSnapshot(uint64_t bytes) { g_layerRectSnapshotBytes += bytes; }
void TVPRecordMetalSurfaceUpload(uint64_t) {}
void TVPRecordMetalSyncWait(uint64_t) { ++g_syncWaits; }
void TVPRecordMetalQueueWait(uint64_t) {}
void TVPRecordMetalRingSuballoc(uint64_t, uint64_t, uint64_t) {}
void TVPRecordMetalRingWrap() {}
void TVPRecordMetalRingStall(uint64_t) {}
void TVPRecordMetalRingFallback(uint64_t) {}
}
bool TVPSoftwareRenderBackendAvailable() { return true; }
void TVPCreateTextureBackend(TVPSprite&) { throw std::runtime_error("unexpected software presenter"); }
void TVPUpdateTextureBackend(TVPSprite*, uint8_t*, int, int, int) { throw std::runtime_error("unexpected software presenter"); }
void TVPDestroyTextureBackend(TVPSprite*) { throw std::runtime_error("unexpected software presenter"); }
void TVPRenderTextureBackend(TVPSprite*, int, int, int, int) { throw std::runtime_error("unexpected software presenter"); }
void TVPRenderClearBackend() { throw std::runtime_error("unexpected software presenter"); }
void TVPRenderPresentBackend() { throw std::runtime_error("unexpected software presenter"); }

namespace
{
void Require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
void DiagnosticAttributionTests()
{
    using krkrsdl3::metal_diagnostics::Workload;
    Workload workload;
    Require(std::strcmp(workload.Bucket(), "empty") == 0, "empty GPU workload attribution");
    const uint32_t stages[] = {Workload::Mesh, Workload::Layer, Workload::Blit,
                               Workload::Window, Workload::OtherRender};
    const char* buckets[] = {"mesh_or_clear", "layer_compute", "blit", "window_render", "other_render"};
    for (size_t i = 0; i < 5; ++i) {
        workload.stages = stages[i];
        Require(std::strcmp(workload.Bucket(), buckets[i]) == 0, "single-stage GPU attribution");
        for (size_t j = i + 1; j < 5; ++j) {
            workload.stages = stages[i] | stages[j];
            Require(std::strcmp(workload.Bucket(), "mixed") == 0,
                    "mixed GPU command buffers must not be attributed to one stage");
        }
    }
    // Draw counts are workload context, never weights for dividing GPU time.
    workload.stages = Workload::Mesh | Workload::Blit | Workload::Layer;
    workload.deformDraws = 100000;
    workload.blitEncoders = 1;
    Require(std::strcmp(workload.Bucket(), "mixed") == 0, "unequal work still needs mixed attribution");
    krkrsdl3::metal_diagnostics::Sampler sampler;
    Require(!sampler.ShouldSample(false, 1), "disabled diagnostics must not sample");
    Require(sampler.ShouldSample(true, 1), "first enabled command buffer is sampled");
    Require(!sampler.ShouldSample(true, 1000000000ULL), "sample rate is bounded below one second");
    Require(sampler.ShouldSample(true, 1000000001ULL), "sample becomes eligible at one second");
    Require(!sampler.ShouldSample(false, 2000000001ULL), "disabled diagnostics stay silent");
    Require(sampler.ShouldSample(true, 2000000001ULL), "re-enabled diagnostics can resume sampling");
    Require(!sampler.ShouldSample(true, 2000000001ULL), "same timestamp cannot sample twice");
}
void StageTimingTests()
{
    namespace timing = krkrsdl3::metal_diagnostics::stage_timing;
    timing::Plan plan;
    Require(plan.ReserveRender(timing::RenderKind::Mesh) == 0, "mesh stage indices");
    Require(plan.ReserveRender(timing::RenderKind::Window) == 4, "window stage indices");
    Require(plan.ReserveRender(timing::RenderKind::Other) == 8, "other render stage indices");
    Require(plan.ReserveEncoder(timing::Stage::LayerCompute) == 12, "compute stage indices");
    Require(plan.ReserveEncoder(timing::Stage::Blit) == 14, "blit stage indices");
    Require(plan.sampleCount == 16, "render passes reserve vertex and fragment intervals atomically");
    // Large absolute clocks, small differences, and a non-nanosecond GPU clock.
    const timing::ClockCalibration clock{1000000000000000000ULL, 1000,
                                         1000000001000000000ULL, 1001000};
    Require(clock.Valid() && clock.NanosecondsPerTick() == 1000.0, "GPU clock calibration slope");
    const uint64_t timestamps[] = {1100, 1300, 1200, 1500, 1400, 1600, 1500, 1900,
                                   2000, 2000, 2500, 2800, 3000, 4000, 3900, 4200};
    auto summary = timing::Resolve(plan, timestamps, 16, clock);
    Require(summary.calibrationValid && summary.valid == 8 && summary.invalid == 0, "complete stage samples");
    const double expected[] = {0.2, 0.3, 0.2, 0.4, 0.0, 0.3, 1.0, 0.3};
    for (size_t i = 0; i < timing::StageCount; ++i) {
        Require(summary.stages[i].valid == 1 && summary.stages[i].invalid == 0, "stage classification");
        Require(std::abs(summary.stages[i].Milliseconds() - expected[i]) < 1e-9, "calibrated stage duration");
    }
    // Vertex/fragment and adjacent encoders can overlap; do not serialize,
    // clip, or reweight measured intervals to make a fictitious frame budget.
    timing::Plan overlap;
    overlap.ReserveEncoder(timing::Stage::LayerCompute);
    overlap.ReserveEncoder(timing::Stage::LayerCompute);
    const uint64_t overlapping[] = {1000, 1001000, 1000, 1001000};
    auto overlappingResult = timing::Resolve(overlap, overlapping, 4, clock);
    Require(overlappingResult.stages[6].Milliseconds() == 2000.0 && overlappingResult.valid == 2,
            "overlapping intervals retain their independent durations");

    summary = timing::Resolve(plan, timestamps, 15, clock);
    Require(summary.valid == 7 && summary.invalid == 1 && summary.stages[7].Milliseconds() == -1.0,
            "truncated results invalidate an incomplete interval, not report zero");
    summary = timing::Resolve(plan, nullptr, 16, clock);
    Require(summary.valid == 0 && summary.invalid == 8, "missing counter data");
    summary = timing::Resolve(timing::Plan{}, nullptr, 0, clock);
    Require(summary.valid == 0 && summary.invalid == 0 && summary.stages[0].Milliseconds() == -1.0,
            "absent stages are not measured zero durations");

    timing::Plan one;
    one.ReserveEncoder(timing::Stage::Blit);
    const uint64_t bad[][2] = {{0, 1200}, {1100, 0}, {1200, 1100}, {999, 1200},
                               {1100, 1001001}, {UINT64_MAX, UINT64_MAX}, {1100, UINT64_MAX}};
    for (const auto& pair : bad) {
        auto invalid = timing::Resolve(one, pair, 2, clock);
        Require(invalid.valid == 0 && invalid.invalid == 1 && invalid.stages[7].Milliseconds() == -1.0,
                "zero, reversed, out-of-range and error timestamps are unavailable");
    }
    const uint64_t customError[] = {1100, 1234};
    Require(timing::Resolve(one, customError, 2, clock, 1234).invalid == 1, "driver error sentinel");
    for (const auto& invalidClock : {timing::ClockCalibration{},
                                     timing::ClockCalibration{2, 1000, 1, 2000},
                                     timing::ClockCalibration{1, 2000, 2, 1000},
                                     timing::ClockCalibration{1, 1000, 2, 1000}}) {
        auto invalid = timing::Resolve(plan, timestamps, 16, invalidClock);
        Require(!invalid.calibrationValid && invalid.valid == 0 && invalid.invalid == 8,
                "missing, reversed and zero-length calibration must fail closed");
    }

    timing::Plan bounded;
    for (uint32_t i = 0; i < timing::MaxSamples / 2 - 1; ++i)
        Require(bounded.ReserveEncoder(timing::Stage::Blit) == i * 2, "bounded sample reservations");
    Require(bounded.ReserveRender(timing::RenderKind::Mesh) == timing::NoSample &&
                bounded.sampleCount == timing::MaxSamples - 2 && bounded.droppedPasses == 1,
            "insufficient space must not partially reserve a render pass");
    Require(bounded.ReserveEncoder(timing::Stage::LayerCompute) == timing::MaxSamples - 2,
            "remaining pair can still measure a compute pass");
    Require(bounded.ReserveEncoder(timing::Stage::Blit) == timing::NoSample && bounded.droppedPasses == 2,
            "sample limit is bounded without another command buffer");
    bounded.Rollback(timing::MaxSamples - 2);
    Require(bounded.sampleCount == timing::MaxSamples - 2 && bounded.failedPasses == 1,
            "failed sampled encoder returns its reserved pair");
    timing::Plan failedRender;
    failedRender.ReserveRender(timing::RenderKind::Mesh);
    failedRender.Rollback(0);
    Require(failedRender.sampleCount == 0 && failedRender.failedPasses == 1 &&
                failedRender.ReserveRender(timing::RenderKind::Window) == 0 &&
                failedRender.stages[0] == timing::Stage::WindowVertex &&
                failedRender.stages[1] == timing::Stage::WindowFragment,
            "render fallback rolls back both stages before indices are reused");
}
void PointQueryScopeTests()
{
    namespace trace = krkrsdl3::point_trace;
    trace::SetEnabled(true);
    trace::Query outer;
    {
        trace::QueryScope outerScope(outer);
        Require(trace::CurrentQuery() == &outer && outer.queryID != 0, "outer point query context");
        trace::Query inner;
        try {
            trace::QueryScope innerScope(inner);
            Require(trace::CurrentQuery() == &inner && inner.queryID != outer.queryID,
                    "nested point query must have its own identity");
            // Model metadata returned by a synchronous backend. It must remain
            // on the caller's object after scope unwinding, never on its parent.
            auto* current = trace::CurrentQuery();
            current->reported = true;
            current->lastSubmittedID = 123;
            current->renderFrame = 45;
            current->wallNS = 9000000;
            current->gpuWaitNS = 8500000;
            current->finishedNS = 10000000;
            throw std::runtime_error("unwind query scope");
        } catch (const std::runtime_error&) {}
        Require(trace::CurrentQuery() == &outer && !outer.reported,
                "point query unwind must restore the parent without leaking metadata");
        Require(inner.reported && inner.lastSubmittedID == 123 && inner.renderFrame == 45 &&
                    inner.wallNS == 9000000 && inner.gpuWaitNS == 8500000 && inner.finishedNS == 10000000,
                "completed point metadata survives scope unwinding");
        trace::SetEnabled(false);
        Require(trace::CurrentQuery() == nullptr, "disabled tracing must hide an active query");
        trace::SetEnabled(true);
    }
    Require(trace::CurrentQuery() == nullptr, "point query must not survive its stack scope");
    trace::SetEnabled(false);
}
std::vector<uint8_t> Read(iTVPRenderBackend& backend, void* target)
{
    int pitch = 0;
    auto* pixels = backend.LockTarget(target, pitch);
    Require(pixels && pitch == 7 * 4, "target readback/pitch");
    std::vector<uint8_t> result(pixels, pixels + pitch * 5);
    backend.UnlockTarget(target);
    return result;
}
void Compare(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int tolerance)
{
    Require(actual.size() == expected.size(), "pixel count differs");
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(int(actual[i]) - int(expected[i])) > tolerance) {
            std::cerr << "pixel byte " << i << ": " << int(actual[i]) << " vs " << int(expected[i]) << '\n';
            throw std::runtime_error("pixel comparison failed");
        }
    }
}
std::vector<uint8_t> Pattern(int pitch, int seed)
{
    std::vector<uint8_t> pixels(pitch * 5, 0xee);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 7; ++x)
            for (int c = 0; c < 4; ++c)
                pixels[y * pitch + x * 4 + c] = uint8_t((y * 47 + x * 29 + c * 63 + seed) & 255);
    return pixels;
}
void TransferTests(iTVPRenderBackend& backend)
{
    const auto pixels = Pattern(36, 11);
    void* target = backend.CreateTarget(7, 5);
    Require(target != nullptr, "create target");
    backend.UpdateTargetTexture(target, pixels.data(), 7, 5, 36);
    std::vector<uint8_t> expected(7 * 5 * 4);
    for (int y = 0; y < 5; ++y) std::memcpy(expected.data() + y * 28, pixels.data() + y * 36, 28);
    Compare(Read(backend, target), expected, 0);
    backend.SetTarget(target);
    backend.ClearTarget(false);
    Compare(Read(backend, target), expected, 0);
    backend.ClearTarget(true);
    Compare(Read(backend, target), std::vector<uint8_t>(7 * 5 * 4, 0), 0);
    Require(backend.GetTargetTexture(target) != nullptr, "target sampling alias");
    backend.DestroyTarget(target);
    int pitch = 42;
    Require(!backend.LockTarget(nullptr, pitch), "null target readback must fail");
    Require(!backend.CreateTarget(0, 5), "invalid dimensions must fail");
}
#ifdef TEST_NATIVE_METAL
void LayerTests(iTVPRenderBackend& gpu)
{
    krkrsdl3::SWRenderBackend cpu;
    void* gt = gpu.CreateTarget(7, 5);
    void* ct = cpu.CreateTarget(7, 5);
    void* gs = gpu.CreateTexture(7, 5);
    void* cs = cpu.CreateTexture(7, 5);
    Require(gt && ct && gs && cs, "layer resources");
    auto src = Pattern(36, 51), dst = Pattern(32, 97);
    gpu.UpdateTexture(gs, src.data(), 7, 5, 36);
    cpu.UpdateTexture(cs, src.data(), 7, 5, 36);
    const float fill[] = {0.2f, 0.4f, 0.8f, 0.6f};
    // Includes the shifted signed arithmetic, alpha preservation, clipping,
    // nonzero UV origins and reversed UVs which fixed-function blending misses.
    for (int method = 0; method <= 10; ++method) {
        for (int opa : {0, 1, 127, 128, 254, 255}) {
            for (bool crop : {false, true}) {
                gpu.UpdateTargetTexture(gt, dst.data(), 7, 5, 32);
                cpu.UpdateTargetTexture(ct, dst.data(), 7, 5, 32);
                gpu.SetTarget(gt); cpu.SetTarget(ct);
                gpu.LayerSetBlend(method, opa / 255.0f, fill);
                cpu.LayerSetBlend(method, opa / 255.0f, fill);
                if (crop) {
                    gpu.LayerDrawRect(gs, -1.25f, 0.5f, 6.75f, 3.25f, 0.8f, 0.2f, 0.1f, 0.9f);
                    cpu.LayerDrawRect(cs, -1.25f, 0.5f, 6.75f, 3.25f, 0.8f, 0.2f, 0.1f, 0.9f);
                } else {
                    gpu.LayerDrawRect(gs, 0, 0, 7, 5);
                    cpu.LayerDrawRect(cs, 0, 0, 7, 5, 0, 0, 1, 1);
                }
                std::cout << "Layer " << method << " opacity " << opa << " crop " << crop << '\n';
                Compare(Read(gpu, gt), Read(cpu, ct), 1);
            }
        }
    }
    // A small non-aliased rectangle must never copy the full attachment on
    // devices without Tier-2 read/write support (Tier-2 needs no snapshot).
    gpu.UpdateTargetTexture(gt, dst.data(), 7, 5, 32);
    cpu.UpdateTargetTexture(ct, dst.data(), 7, 5, 32);
    gpu.LayerSetBlend(iTVPRenderBackend::LBM_ALPHA, 1, nullptr);
    cpu.LayerSetBlend(iTVPRenderBackend::LBM_ALPHA, 1, nullptr);
    const auto snapshotBytes = krkrsdl3::g_layerRectSnapshotBytes;
    gpu.LayerDrawRect(gs, 1, 1, 2, 2, 0, 0, 1, 1);
    cpu.LayerDrawRect(cs, 1, 1, 2, 2, 0, 0, 1, 1);
    Require(krkrsdl3::g_layerRectSnapshotBytes - snapshotBytes <= 2 * 2 * 4,
            "clipped LayerDrawRect copied more than its affected rectangle");
    Compare(Read(gpu, gt), Read(cpu, ct), 1);
    // A copy of the same attachment must sample a snapshot, never a read/write hazard.
    gpu.UpdateTargetTexture(gt, dst.data(), 7, 5, 32);
    auto before = Read(gpu, gt);
    gpu.LayerSetBlend(iTVPRenderBackend::LBM_COPY, 1, nullptr);
    gpu.LayerDrawRect(gpu.GetTargetTexture(gt), 0, 0, 7, 5);
    Compare(Read(gpu, gt), before, 0);
    gpu.DestroyTexture(gs); cpu.DestroyTexture(cs);
    gpu.DestroyTarget(gt); cpu.DestroyTarget(ct);
}
void MeshTests(iTVPRenderBackend& gpu)
{
    void* target = gpu.CreateTarget(7, 5);
    void* mask = gpu.CreateTarget(7, 5);
    void* src = gpu.CreateTexture(1, 1);
    Require(target && mask && src, "mesh resources");
    const uint8_t color[] = {80, 160, 240, 128};
    const uint8_t destination[] = {40, 60, 100, 90};
    const float uniform[] = {0.2f, 0.4f, 0.8f, 0.6f};
    const float modulation[] = {0.5f, 1, 0.5f, 1};
    gpu.UpdateTexture(src, color, 1, 1, 4);
    std::vector<uint8_t> background(7 * 5 * 4), stencil(background.size());
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) {
        std::memcpy(background.data() + (y * 7 + x) * 4, destination, 4);
        stencil[(y * 7 + x) * 4 + 3] = x < 3 ? 127 : 128;
    }
    gpu.UpdateTargetTexture(mask, stencil.data(), 7, 5, 28);
    const float vertices[] = {-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const uint16_t indices[] = {0,1,2, 2,3,0};
    gpu.SetTarget(target);
    for (int mode : {0, 1, 3, 4, 6, 21}) {
        gpu.UpdateTargetTexture(target, background.data(), 7, 5, 28);
        gpu.SetMask(mask);
        gpu.SetBlendMode(mode, uniform);
        gpu.DrawMesh(vertices, 4, indices, 6, src, 0.75f, modulation);
        auto actual = Read(gpu, target);
        for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) {
            float s[4] = {80/255.0f, 160/255.0f, 240/255.0f, 128/255.0f};
            if (mode == 21) { for (int c = 0; c < 3; ++c) s[c] = uniform[c]; s[3] *= uniform[3]; }
            for (int c = 0; c < 4; ++c) s[c] *= modulation[c];
            s[3] *= 0.75f;
            for (int c = 0; c < 4; ++c) {
                float d = destination[c] / 255.0f;
                float result = d;
                if (x >= 3 && mode != 6) {
                    if (mode == 1 || mode == 4) result = c == 3 ? d : s[c] * d + d;
                    else if (mode == 21) result = s[c] * s[3] + d * (1 - s[3]);
                    else result = c == 3 ? std::max(s[3], d) : s[c] * s[3] + d * (1 - s[3]);
                }
                int expected = int(std::clamp(result, 0.0f, 1.0f) * 255 + 0.5f);
                Require(std::abs(int(actual[(y * 7 + x) * 4 + c]) - expected) <= 1, "mesh blending/mask threshold");
            }
        }
    }
    gpu.SetMask(nullptr);
    gpu.DestroyTexture(src); gpu.DestroyTarget(mask); gpu.DestroyTarget(target);
}
void MeshBatchTests(iTVPRenderBackend& gpu)
{
    void* target = gpu.CreateTarget(7, 5);
    void* source = gpu.CreateTexture(1, 1);
    Require(target && source, "mesh batch resources");
    const uint8_t pixel[] = {255, 90, 30, 128};
    const float vertices[] = {-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const uint16_t indices[] = {0,1,2, 2,3,0};
    gpu.UpdateTexture(source, pixel, 1, 1, 4);
    gpu.SetTarget(target);
    gpu.SetMask(nullptr);
    gpu.SetBlendMode(0, nullptr);
    const int before = krkrsdl3::g_renderEncoders;
    gpu.ClearTarget(true);
    for (int i = 0; i < 12; ++i)
        gpu.DrawMesh(vertices, 4, indices, 6, source, 1.0f);
    const auto pixels = Read(gpu, target); // Ends the active encoder before GPU readback.
    Require(krkrsdl3::g_renderEncoders - before == 1,
            "clear and consecutive same-target Emote draws should share one render encoder");
    Require(pixels[0] != 0 || pixels[1] != 0, "batched mesh draws produced no pixels");
    gpu.DestroyTexture(source);
    gpu.DestroyTarget(target);
}
void ClearMeshOrderingTests(iTVPRenderBackend& gpu)
{
    void* target = gpu.CreateTarget(7, 5);
    void* other = gpu.CreateTarget(7, 5);
    void* destination = gpu.CreateLayerTexture(7, 5, TVPLayerTextureFormat::RGBA8);
    void* source = gpu.CreateTexture(1, 1);
    Require(target && other && destination && source, "clear ordering resources");
    const uint8_t pixel[] = {255, 90, 30, 255};
    const float vertices[] = {-1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
    const uint16_t indices[] = {0,1,2, 2,3,0};
    const std::vector<uint8_t> clearPixels(7 * 5 * 4, 0);
    std::vector<uint8_t> drawnPixels(7 * 5 * 4);
    for (size_t i = 0; i < drawnPixels.size(); i += 4)
        std::memcpy(drawnPixels.data() + i, pixel, 4);
    gpu.UpdateTexture(source, pixel, 1, 1, 4);
    gpu.SetMask(nullptr);
    gpu.SetBlendMode(0, nullptr);

    // Switching targets must preserve the preceding clear/draw pass. Clearing
    // an already active target must discard its previous draws, even twice.
    gpu.SetTarget(target);
    gpu.ClearTarget(true);
    gpu.DrawMesh(vertices, 4, indices, 6, source, 1.0f);
    gpu.SetTarget(other);
    gpu.ClearTarget(true);
    gpu.DrawMesh(vertices, 4, indices, 6, source, 1.0f);
    gpu.SetTarget(target);
    gpu.ClearTarget(true);
    gpu.ClearTarget(true);
    Require(gpu.CopyTargetToLayerTexture(target, destination), "copy immediately after clear");
    std::vector<uint8_t> copied;
    int pitch = 0;
    Require(gpu.ReadLayerTexture(destination, copied, pitch) && pitch == 28,
            "clear-only pass followed by blit/readback");
    Compare(copied, clearPixels, 0);
    Compare(Read(gpu, other), drawnPixels, 0);
    Compare(Read(gpu, target), clearPixels, 0);

    // A mask generated by clear + draw must be ready when the next target
    // samples it, without a readback or submission between the two passes.
    gpu.SetTarget(other);
    gpu.ClearTarget(true);
    gpu.DrawMesh(vertices, 4, indices, 6, source, 1.0f);
    gpu.SetTarget(target);
    gpu.SetMask(other);
    gpu.ClearTarget(true);
    gpu.DrawMesh(vertices, 4, indices, 6, source, 1.0f);
    Compare(Read(gpu, target), drawnPixels, 0);
    gpu.SetMask(nullptr);

    // A compute operation following a retained clear pass must see zeroes in
    // the destination, rather than the contents from before that clear.
    gpu.UpdateTargetTexture(target, drawnPixels.data(), 7, 5, 28);
    gpu.ClearTarget(true);
    gpu.LayerSetBlend(3, 1.0f, nullptr);
    gpu.LayerDrawRect(source, 0, 0, 7, 5, 0, 0, 1, 1);
    auto computePixels = drawnPixels;
    for (size_t i = 0; i < computePixels.size(); i += 4) {
        for (size_t c = 0; c < 3; ++c) computePixels[i + c] = (pixel[c] * 255) >> 8;
        computePixels[i + 3] = 0; // Additive Layer blending retains destination alpha.
    }
    Compare(Read(gpu, target), computePixels, 0);

    // The accelerated deformation path uses the same clear pass. This single
    // affine surface maps the UV grid onto the full target without a mask.
    krkrsdl3::TVPMeshDeformSurface surface;
    surface.type = 2;
    surface.matrix[0] = surface.matrix[5] = 2;
    surface.matrix[10] = surface.matrix[15] = 1;
    surface.matrix[12] = surface.matrix[13] = -1;
    const int before = krkrsdl3::g_renderEncoders;
    gpu.ClearTarget(true);
    Require(gpu.DrawDeformedMesh(2, 2, &surface, 1, source, 1.0f), "deformed draw after clear");
    Compare(Read(gpu, target), drawnPixels, 0);
    Require(krkrsdl3::g_renderEncoders - before == 1,
            "clear and deformed mesh should share one render encoder");

    gpu.DestroyTexture(source);
    gpu.DestroyLayerTexture(destination);
    gpu.DestroyTarget(other);
    gpu.DestroyTarget(target);
}
void DirectLayerCopyTests(iTVPRenderBackend& gpu)
{
    void* source = gpu.CreateTarget(7, 5);
    void* destination = gpu.CreateLayerTexture(7, 5, TVPLayerTextureFormat::RGBA8);
    Require(source && destination, "direct Layer copy resources");
    auto pixels = Pattern(32, 123);
    gpu.UpdateTargetTexture(source, pixels.data(), 7, 5, 32);
    Require(gpu.CopyTargetToLayerTexture(source, destination), "target->Layer GPU copy");
    std::vector<uint8_t> actual;
    int pitch = 0;
    Require(gpu.ReadLayerTexture(destination, actual, pitch) && pitch == 28,
            "direct Layer copy readback");
    std::vector<uint8_t> expected(7 * 5 * 4);
    for (int y = 0; y < 5; ++y)
        std::memcpy(expected.data() + y * 28, pixels.data() + y * 32, 28);
    Compare(actual, expected, 0);

    void* wrong = gpu.CreateLayerTexture(6, 5, TVPLayerTextureFormat::RGBA8);
    Require(wrong && !gpu.CopyTargetToLayerTexture(source, wrong),
            "mismatched target->Layer copy must reject");
    gpu.DestroyLayerTexture(wrong);
    gpu.DestroyLayerTexture(destination);
    gpu.DestroyTarget(source);
}
void CaptureTests(iTVPRenderBackend& gpu)
{
    void* texture = gpu.CreateWindowTexture(1, 2);
    const uint8_t colors[] = {255,0,0,0, 0,0,255,128};
    gpu.UpdateWindowTexture(texture, colors, 1, 2, 4);
    gpu.BeginFrame(12, 8);
    gpu.DrawWindowTexture(texture, 4, 0, 4, 8);
    gpu.EndFrame(); // Hidden test window: still flush uploads without waiting for a drawable.
    int w = 0, h = 0, pitch = 0;
    std::vector<uint8_t> pixels;
    Require(gpu.CaptureFrame(pixels, w, h, pitch) && w == 12 && h == 8 && pitch == 48, "native screenshot");
    Require(pixels[0] == 0 && pixels[3] == 0, "letterbox background");
    Require(pixels[5 * 4] == 255 && pixels[5 * 4 + 2] == 0 && pixels[5 * 4 + 3] == 255,
            "top red row / opaque presentation of zero-alpha RGB");
    Require(pixels[7 * pitch + 5 * 4] == 0 && pixels[7 * pitch + 5 * 4 + 2] == 255, "bottom blue row / orientation");
    gpu.DestroyWindowTexture(texture);
    Require(!gpu.CaptureFrame(pixels, w, h, pitch), "destroyed screenshot source must not remain retained");
}
void ScopedPointReadTests(iTVPRenderBackend& gpu)
{
    namespace trace = krkrsdl3::point_trace;
    void* texture = gpu.CreateLayerTexture(7, 5, TVPLayerTextureFormat::RGBA8);
    Require(texture != nullptr, "scoped point read texture");
    const auto pixels = Pattern(32, 41);
    Require(gpu.UpdateLayerTexture(texture, pixels.data(), 32, {0, 0, 7, 5}), "scoped point upload");
    trace::SetEnabled(true);
    trace::Query outer;
    {
        trace::QueryScope outerScope(outer);
        trace::Query point;
        point.x = 2; point.y = 3;
        point.width = 7; point.height = 5;
        {
            trace::QueryScope pointScope(point);
            const uint64_t queryID = point.queryID;
            std::vector<uint8_t> actual;
            int pitch = 0;
            Require(gpu.ReadLayerTextureRegion(texture, {2, 3, 3, 4}, actual, pitch) &&
                        pitch == 4 && actual.size() == 4, "scoped native point readback");
            Require(std::memcmp(actual.data(), pixels.data() + 3 * 32 + 2 * 4, 4) == 0,
                    "point diagnostics must preserve readback pixels");
            Require(trace::CurrentQuery() == &point && point.queryID == queryID,
                    "native readback must retain caller's query identity and scope");
            if (point.reported) {
                Require(point.lastSubmittedID != 0 && point.wallNS >= 8000000 &&
                            point.gpuWaitNS <= point.wallNS && point.finishedNS != 0,
                        "naturally slow native readback must return measured metadata");
            } else {
                Require(point.lastSubmittedID == 0 && point.wallNS == 0 &&
                            point.gpuWaitNS == 0 && point.finishedNS == 0,
                        "fast or rate-limited native reads must not mark caller details");
            }
        }
        Require(trace::CurrentQuery() == &outer && !outer.reported,
                "native point read must restore and leave outer query unchanged");
    }
    trace::SetEnabled(false);
    gpu.DestroyLayerTexture(texture);
}
// GPU-to-GPU work (target->Layer blits, Layer compute operations) consumes no
// host-visible staging memory. It previously counted against the 16 MB staging
// budget, so full-surface Emote captures (~8.5 MB each) forced a submit every
// other capture and serialized the CPU against the inFlight semaphore.
void SubmissionCadenceTests(iTVPRenderBackend& gpu)
{
    const int waitsBefore = krkrsdl3::g_syncWaits;
    const int width = 512, height = 512; // 1 MiB per surface
    void* source = gpu.CreateTarget(width, height);
    void* destination = gpu.CreateLayerTexture(width, height, TVPLayerTextureFormat::RGBA8);
    Require(source && destination, "cadence test resources");

    // Prime the source once, then measure only the GPU-to-GPU copies.
    std::vector<uint8_t> seed(size_t(width) * height * 4, 0x40);
    gpu.UpdateTargetTexture(source, seed.data(), width, height, width * 4);

    const int before = krkrsdl3::g_metalSubmits;
    const int copies = 32; // 32 MiB of pixels: twice the old 16 MB byte budget
    for (int i = 0; i < copies; ++i)
        Require(gpu.CopyTargetToLayerTexture(source, destination), "cadence GPU copy");
    const int forced = krkrsdl3::g_metalSubmits - before;

    // Old behavior: >= 2 submits (32 MiB / 16 MB). Fixed: none, because no
    // host-visible bytes were staged and the op budget is far from reached.
    if (forced != 0) {
        std::cerr << "GPU-only copies forced " << forced << " submit(s)" << '\n';
        throw std::runtime_error("GPU-to-GPU work must not consume the staging budget");
    }

    // Staging uploads must still bound the buffer: each upload stages ~1 MiB, so
    // crossing 16 MB has to submit at least once.
    const int beforeUploads = krkrsdl3::g_metalSubmits;
    for (int i = 0; i < 24; ++i)
        gpu.UpdateTargetTexture(source, seed.data(), width, height, width * 4);
    Require(krkrsdl3::g_metalSubmits > beforeUploads,
            "host-visible uploads must still trigger the staging budget");
    Require(krkrsdl3::g_syncWaits == waitsBefore,
            "GPU timing diagnostics must not add synchronous waits");

    gpu.DestroyLayerTexture(destination);
    gpu.DestroyTarget(source);
}
#endif
}

int main()
{
    try {
        DiagnosticAttributionTests();
        StageTimingTests();
        PointQueryScopeTests();
        krkrsdl3::SWRenderBackend software;
        TransferTests(software);
#ifdef TEST_NATIVE_METAL
        if (!krkrsdl3::MetalRenderBackendAvailable()) {
            std::cout << "SKIP: no Metal device; software reference passed, native GPU tests not run\n";
            return 77;
        }
        Require(SDL_Init(SDL_INIT_VIDEO), "SDL init");
        SDL_Window* window = SDL_CreateWindow("Metal backend tests", 12, 8, SDL_WINDOW_METAL | SDL_WINDOW_HIDDEN);
        Require(window != nullptr, "SDL Metal window");
        for (int session = 0; session < 2; ++session) {
            std::unique_ptr<iTVPRenderBackend> gpu(krkrsdl3::MetalRenderBackend::Create(window, false));
            Require(gpu && gpu->IsHardware() && std::strcmp(gpu->GetName(), "metal") == 0, "native backend initialization");
            TransferTests(*gpu);
            if (session == 0) {
                LayerTests(*gpu);
                MeshTests(*gpu);
                SDL_SetHint("MIKAGE_METAL_DIAGNOSTICS", "1");
                MeshBatchTests(*gpu);
                ClearMeshOrderingTests(*gpu);
                DirectLayerCopyTests(*gpu);
                ScopedPointReadTests(*gpu);
                SubmissionCadenceTests(*gpu);
                SDL_SetHint("MIKAGE_METAL_DIAGNOSTICS", "0");
            }
            CaptureTests(*gpu);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "PASS: native Metal transfer, all Layer blends, mesh/mask, capture and submission cadence tests\n";
#else
        std::cout << "PASS: diagnostic attribution/sampling/stage timing and software reference transfer tests; native Metal requires Apple + SDL3 (not tested)\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
