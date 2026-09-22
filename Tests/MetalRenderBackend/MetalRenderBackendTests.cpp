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
void TVPRecordMetalSubmit() {}
void TVPRecordMetalSyncWait(uint64_t) {}
void TVPRecordMetalQueueWait(uint64_t) {}
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
                DirectLayerCopyTests(*gpu);
            }
            CaptureTests(*gpu);
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "PASS: native Metal transfer, all Layer blends, mesh/mask and capture tests\n";
#else
        std::cout << "PASS: software reference transfer tests; native Metal requires Apple + SDL3 (not tested)\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
