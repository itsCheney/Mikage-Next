#include "backend/SWRenderBackend.h"
#ifdef TEST_NATIVE_METAL
#include "backend/MetalRenderBackend.h"
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
uint64_t g_layerRectSnapshotBytes = 0;
void TVPRecordMetalSubmit() { ++g_metalSubmits; }
void TVPRecordMetalRenderEncoder() { ++g_renderEncoders; }
void TVPRecordMetalComputeEncoder() {}
void TVPRecordMetalBlitEncoder() {}
void TVPRecordMetalLayerRectSnapshot(uint64_t bytes) { g_layerRectSnapshotBytes += bytes; }
void TVPRecordMetalSurfaceUpload(uint64_t) {}
void TVPRecordMetalSyncWait(uint64_t) {}
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
// GPU-to-GPU work (target->Layer blits, Layer compute operations) consumes no
// host-visible staging memory. It previously counted against the 16 MB staging
// budget, so full-surface Emote captures (~8.5 MB each) forced a submit every
// other capture and serialized the CPU against the inFlight semaphore.
void SubmissionCadenceTests(iTVPRenderBackend& gpu)
{
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

    gpu.DestroyLayerTexture(destination);
    gpu.DestroyTarget(source);
}
#endif
}

int main()
{
    try {
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
                MeshBatchTests(*gpu);
                ClearMeshOrderingTests(*gpu);
                DirectLayerCopyTests(*gpu);
                SubmissionCadenceTests(*gpu);
            }
            CaptureTests(*gpu);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "PASS: native Metal transfer, all Layer blends, mesh/mask, capture and submission cadence tests\n";
#else
        std::cout << "PASS: software reference transfer tests; native Metal requires Apple + SDL3 (not tested)\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
