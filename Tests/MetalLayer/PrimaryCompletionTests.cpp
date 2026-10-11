#include "tjsCommHead.h"
#include "LayerBitmap.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "drawable.h"
#include "PrimaryCompletion.h"
#include "UpdateRegionCoalescing.h"
#include "TVPMsg.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// Production tTVPDestTexture/tTVPLayerManager::DrawCompleted and both
// iTVPBaseBitmap::Blt overloads are extracted unchanged. The facade holds only
// the manager fields DrawCompleted reads; window/script machinery is absent.
class TestDestTexture : public tTVPBaseTexture {
    bool HoldAlpha = true;
public:
    TestDestTexture(tjs_uint w, tjs_uint h) : tTVPBaseTexture(w, h) {}
    virtual bool CopyRect(tjs_int x, tjs_int y, const iTVPBaseBitmap* ref, tTVPRect refrect,
                          tjs_int plane = (TVP_BB_COPY_MAIN | TVP_BB_COPY_MASK));
    void SetHoldAlpha(bool b) { HoldAlpha = b; }
};
class TestPrimaryManager {
public:
    tTVPComplexRect UpdateRegion;
    tTVPBaseTexture* DrawBuffer = nullptr;
    bool HoldAlpha = true;
    tjs_int primaryWidth = 0, primaryHeight = 0;
    ~TestPrimaryManager() { delete DrawBuffer; }
    bool GetPrimaryLayerSize(tjs_int& w, tjs_int& h) const {
        w = primaryWidth; h = primaryHeight; return w > 0 && h > 0;
    }
    void DrawCompleted(const tTVPRect& destrect, tTVPBaseTexture* bmp, const tTVPRect& cliprect,
                       tTVPLayerType type, tjs_int opacity);
    void NotifyUpdateRegionFixed();
};

#include "ProductionPrimaryCompletion.inc"

namespace {
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::vector<uint32_t> Pattern(int w, int h, uint32_t seed) {
    std::vector<uint32_t> pixels(size_t(w) * h);
    // Alpha varies too, so HoldAlpha/RGB-only paths are distinguishable.
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = seed + uint32_t(i) * 0x0b071305u;
    return pixels;
}
void Write(iTVPTexture2D* texture, const std::vector<uint32_t>& pixels) {
    const int w = int(texture->GetWidth()), h = int(texture->GetHeight());
    texture->Update(pixels.data(), TVPTextureFormat::RGBA, w * 4, tTVPRect(0, 0, w, h));
}
std::vector<uint32_t> Read(iTVPTexture2D* texture) {
    const size_t w = texture->GetWidth(), h = texture->GetHeight();
    std::vector<uint32_t> pixels(w * h);
    for (size_t y = 0; y < h; ++y)
        std::memcpy(pixels.data() + y * w, texture->GetScanLineForRead(unsigned(y)), w * 4);
    return pixels;
}
// Bitmaps bound to an explicit manager: the GPU one feeds DrawCompleted as a
// child MainImage; the software one evaluates the same production Blt.
class ManagedBitmap final : public iTVPBaseBitmap {
    iTVPRenderManager* manager;
public:
    ManagedBitmap(iTVPRenderManager* m, int w, int h, const std::vector<uint32_t>& pixels) : manager(m) {
        Bitmap = m->CreateTexture2D(nullptr, 0, w, h, TVPTextureFormat::RGBA);
        Write(Bitmap, pixels);
    }
    iTVPRenderManager* GetRenderManager() override { return manager; }
};
// Software result of exactly the production Blt that DrawCompleted would issue.
std::vector<uint32_t> Reference(const std::vector<uint32_t>& target, int w, int h, const tTVPRect& destination,
                                const std::vector<uint32_t>* source, int sw, int sh, const tTVPRect& clip,
                                tTVPLayerType type, int opacity, bool holdAlpha) {
    ManagedBitmap out(TVPGetSoftwareRenderManager(), w, h, target);
    if (source) {
        ManagedBitmap in(TVPGetSoftwareRenderManager(), sw, sh, *source);
        out.Blt(destination.left, destination.top, &in, clip, type, opacity, holdAlpha);
    } else {
        // Same-bitmap operands: copy the aliased input first, as the GPU snapshot does.
        ManagedBitmap in(TVPGetSoftwareRenderManager(), w, h, target);
        out.Blt(destination.left, destination.top, &in, clip, type, opacity, holdAlpha);
    }
    return Read(out.GetTexture());
}
struct Work {
    TVPLayerRenderStats before = TVPGetMetalLayerRenderStats();
    bool None() const {
        const auto after = TVPGetMetalLayerRenderStats();
        return after.gpuOperations == before.gpuOperations && after.cpuFallbacks == before.cpuFallbacks &&
            after.uploadedBytes == before.uploadedBytes && after.readbackBytes == before.readbackBytes;
    }
    // GPU or software fallback: either is one composed production operation.
    uint64_t Operations() const {
        const auto after = TVPGetMetalLayerRenderStats();
        return after.gpuOperations + after.cpuFallbacks - before.gpuOperations - before.cpuFallbacks;
    }
};
// Copies are byte exact; Metal blend formulas are accepted within 1 per channel.
bool Matches(const std::vector<uint32_t>& actual, const std::vector<uint32_t>& expected, int tolerance) {
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
        for (int shift = 0; shift < 32; shift += 8) {
            const int a = int((actual[i] >> shift) & 255), e = int((expected[i] >> shift) & 255);
            if (a - e > tolerance || e - a > tolerance) return false;
        }
    return true;
}
void Prepare(TestPrimaryManager& manager, int w, int h, const std::vector<uint32_t>& pixels, bool holdAlpha) {
    manager.primaryWidth = w; manager.primaryHeight = h; manager.HoldAlpha = holdAlpha;
    auto* buffer = new TestDestTexture(w, h); buffer->SetHoldAlpha(holdAlpha);
    manager.DrawBuffer = buffer;
    Write(buffer->GetTexture(), pixels);
    buffer->GetTexture()->GetTextureHandle(); // settle the upload before measuring
}
}

void PrimaryCompletionTests() {
    using krkrsdl3::primary_completion::IsIdentity;
    constexpr int w = 13, h = 9;
    const auto pixels = Pattern(w, h, 0x8040c020u);
    const tTVPRect full(0, 0, w, h), sliver(2, 7, 11, 8), glyph(4, 1, 7, 9);
    int buffer = 0, other = 0;
    // The predicate accepts only the self-copy the opaque Primary produces.
    Require(IsIdentity(&buffer, &buffer, sliver, sliver, ltOpaque, 255), "identity self-copy rejected");
    Require(!IsIdentity(nullptr, nullptr, sliver, sliver, ltOpaque, 255), "absent DrawBuffer accepted");
    Require(!IsIdentity(&buffer, &other, sliver, sliver, ltOpaque, 255), "child image accepted");
    Require(!IsIdentity(&buffer, &buffer, sliver, tTVPRect(3, 7, 12, 8), ltOpaque, 255), "shifted source accepted");
    Require(!IsIdentity(&buffer, &buffer, sliver, tTVPRect(2, 7, 10, 8), ltOpaque, 255), "resized source accepted");
    Require(!IsIdentity(&buffer, &buffer, sliver, sliver, ltOpaque, 254), "partial opacity accepted");
    for (auto type : {ltBinder, ltAlpha, ltAdditive, ltAddAlpha, ltPsNormal})
        Require(!IsIdentity(&buffer, &buffer, sliver, sliver, type, 255), "blending layer type accepted");

    for (bool holdAlpha : {true, false}) {
        // Opaque Primary completion of its own DrawBuffer: no backend work at
        // all (no snapshot, operation, upload or readback) and unchanged pixels.
        // With HoldAlpha=false a performed copy would reach the fixture's
        // throwing iTVPBaseBitmap::CopyRect.
        TestPrimaryManager manager; Prepare(manager, w, h, pixels, holdAlpha);
        for (const auto& rect : {full, sliver, glyph}) {
            Work work;
            manager.DrawCompleted(rect, manager.DrawBuffer, rect, ltOpaque, 255);
            Require(work.None(), "identity Primary completion encoded backend work");
        }
        Require(Read(manager.DrawBuffer->GetTexture()) == pixels, "identity Primary completion changed pixels");
        if (!holdAlpha) {
            bool reached = false;
            try { manager.DrawCompleted(sliver, manager.DrawBuffer, tTVPRect(1, 0, 10, 1), ltOpaque, 255); }
            catch (const std::runtime_error&) { reached = true; }
            Require(reached, "shifted HoldAlpha=false self-copy skipped its production CopyRect");
        }
    }

    // Every non-identity completion still runs exactly one production Blt and
    // matches the software result: shifted self-copy, partial opacity,
    // blending type, and a child MainImage.
    struct Case { tTVPRect destination, clip; tTVPLayerType type; int opacity; bool child; };
    const Case cases[] = {
        {tTVPRect(0, 5, 6, 8), tTVPRect(7, 0, 13, 3), ltOpaque, 255, false},
        {sliver, sliver, ltOpaque, 128, false},
        {glyph, glyph, ltAlpha, 255, false},
        {tTVPRect(3, 2, 9, 6), tTVPRect(1, 1, 7, 5), ltOpaque, 255, true},
        {tTVPRect(3, 2, 9, 6), tTVPRect(1, 1, 7, 5), ltAlpha, 200, true},
    };
    const auto childPixels = Pattern(8, 6, 0x13579bdfu);
    for (const auto& c : cases) {
        TestPrimaryManager manager; Prepare(manager, w, h, pixels, true);
        tTVPBaseTexture* source = manager.DrawBuffer;
        std::unique_ptr<tTVPBaseTexture> childTexture;
        if (c.child) {
            childTexture.reset(new tTVPBaseTexture(8, 6));
            Write(childTexture->GetTexture(), childPixels);
            childTexture->GetTexture()->GetTextureHandle();
            source = childTexture.get();
        }
        const auto expected = Reference(pixels, w, h, c.destination, c.child ? &childPixels : nullptr,
                                        8, 6, c.clip, c.type, c.opacity, true);
        Work work;
        manager.DrawCompleted(c.destination, source, c.clip, c.type, c.opacity);
        Require(work.Operations() == 1, "non-identity Primary completion was not composed once");
        const bool copy = c.type == ltOpaque && c.opacity == 255;
        Require(Matches(Read(manager.DrawBuffer->GetTexture()), expected, copy ? 0 : 1),
                "non-identity Primary completion pixels changed");
    }

    // Lazy creation and growth keep their production black fill; a following
    // identity completion keeps it and performs no work.
    {
        TestPrimaryManager manager; manager.primaryWidth = 5; manager.primaryHeight = 4;
        tTVPBaseTexture child(5, 4);
        Write(child.GetTexture(), Pattern(5, 4, 0x01020304u));
        manager.DrawCompleted(tTVPRect(0, 0, 5, 4), &child, tTVPRect(0, 0, 5, 4), ltOpaque, 255);
        Require(manager.DrawBuffer && manager.DrawBuffer->GetWidth() == 5, "DrawBuffer was not created");
        manager.primaryWidth = 7; manager.primaryHeight = 6;
        auto* before = manager.DrawBuffer;
        manager.DrawCompleted(tTVPRect(0, 0, 7, 6), before, tTVPRect(0, 0, 7, 6), ltOpaque, 255);
        Require(manager.DrawBuffer == before && manager.DrawBuffer->GetWidth() == 8 &&
                manager.DrawBuffer->GetHeight() == 6, "DrawBuffer growth changed");
        for (uint32_t pixel : Read(manager.DrawBuffer->GetTexture()))
            Require(pixel == 0xff000000u, "grown DrawBuffer lost its black fill");
        Work work;
        manager.DrawCompleted(tTVPRect(0, 0, 7, 6), manager.DrawBuffer, tTVPRect(0, 0, 7, 6), ltOpaque, 255);
        Require(work.None(), "identity completion after growth encoded work");
    }
    std::cout << "PASS Primary DrawBuffer identity completion skipped; shifted/opacity/blend/child completions unchanged\n";
}

namespace {
using krkrsdl3::update_region::Coalesce;
using krkrsdl3::update_region::DefaultPassPixels;
std::vector<tTVPRect> Rects(const tTVPComplexRect& region) {
    std::vector<tTVPRect> rects;
    auto it = region.GetIterator();
    while (it.Step()) rects.push_back(*it);
    return rects;
}
tTVPComplexRect Region(const std::vector<tTVPRect>& rects) {
    tTVPComplexRect region;
    for (const auto& r : rects) region.Or(r);
    return region;
}
bool Same(std::vector<tTVPRect> a, std::vector<tTVPRect> b) {
    auto order = [](const tTVPRect& x, const tTVPRect& y) { return x < y || (!(y < x) && x.right < y.right); };
    std::sort(a.begin(), a.end(), order); std::sort(b.begin(), b.end(), order);
    return a == b;
}
// Disjoint output that covers every input pixel and stays inside the bound.
void Invariants(const tTVPComplexRect& input, const tTVPComplexRect& output, uint64_t passPixels) {
    const auto in = Rects(input), out = Rects(output);
    for (size_t a = 0; a < out.size(); ++a)
        for (size_t b = a + 1; b < out.size(); ++b)
            Require(!out[a].intersects_with_no_empty_check(out[b]), "coalesced update rectangles overlap");
    for (const auto& r : in) {
        uint64_t covered = 0;
        for (const auto& o : out) { tTVPRect i; if (TVPIntersectRect(&i, r, o)) covered += krkrsdl3::update_region::Area(i); }
        Require(covered == krkrsdl3::update_region::Area(r), "coalescing dropped an updated pixel");
    }
    const tTVPRect bound = input.GetBound();
    for (const auto& o : out) Require(o.included_in_no_empty_check(bound), "coalescing grew past the update bound");
    uint64_t inPixels = 0, outPixels = 0;
    for (const auto& r : in) inPixels += krkrsdl3::update_region::Area(r);
    for (const auto& o : out) outPixels += krkrsdl3::update_region::Area(o);
    Require(out.size() <= in.size() &&
            krkrsdl3::update_region::Cost(out.size(), outPixels, passPixels) <=
            krkrsdl3::update_region::Cost(in.size(), inPixels, passPixels), "coalescing raised modeled cost");
}
}

void UpdateRegionCoalescingTests(bool metalBound) {
    if (metalBound) {
        // Update rectangles retained from After_P2D_3 device samples (Primary
        // completions of one command buffer), after ComplexRect banding.
        struct Fixture { const char* name; std::vector<tTVPRect> input, expected; };
        const Fixture fixtures[] = {
            // Angel cmd 2476: message line plus the 1px row its next glyph adds.
            {"line+sliver", {{450, 830, 1242, 882}, {450, 882, 463, 883}}, {{450, 830, 1242, 883}}},
            // Angel cmd 871: adjacent glyph cells (Or already joins equal bands);
            // the distant HUD strip stays apart.
            {"glyph cells", {{0, 18, 462, 45}, {463, 882, 502, 950}, {502, 882, 541, 950}},
                            {{0, 18, 462, 45}, {463, 882, 541, 950}}},
            // DRACU cmd 1444: glyph plus 2px sliver; the 1x1 far corner stays apart.
            {"corner apart", {{0, 0, 1, 1}, {375, 580, 402, 607}, {402, 582, 404, 607}},
                             {{0, 0, 1, 1}, {375, 580, 404, 607}}},
            // 9-nine cmd 1946: one row of glyphs split into cells.
            {"glyph row", {{353, 573, 380, 601}, {380, 573, 407, 601}, {407, 573, 435, 601}, {543, 573, 569, 601}},
                          {{353, 573, 569, 601}}},
            // Chiren cmd 680: glyph with slivers; the distant button stays apart.
            {"button apart", {{585, 896, 624, 932}, {624, 898, 626, 932}, {587, 932, 626, 934}, {1635, 976, 1835, 1024}},
                             {{585, 896, 626, 934}, {1635, 976, 1835, 1024}}},
            // Unrelated full-width and corner regions are not merged.
            {"far apart", {{0, 0, 1920, 120}, {1800, 1000, 1920, 1080}}, {{0, 0, 1920, 120}, {1800, 1000, 1920, 1080}}},
        };
        for (const auto& f : fixtures) {
            auto region = Region(f.input);
            const auto input = Region(f.input);
            const auto result = Coalesce(region);
            Invariants(input, region, DefaultPassPixels);
            if (!Same(Rects(region), f.expected)) {
                std::cerr << "fixture " << f.name << " produced";
                for (const auto& r : Rects(region)) std::cerr << " [" << r.left << ',' << r.top << ',' << r.right << ',' << r.bottom << ']';
                std::cerr << '\n';
                throw std::runtime_error("device update-region fixture coalesced differently");
            }
            Require(result.rectanglesBefore == size_t(input.GetCount()) &&
                    result.rectanglesAfter == size_t(region.GetCount()), "coalescing result counts differ");
        }
        // Empty, single and zero-weight regions are untouched.
        {
            tTVPComplexRect empty; Coalesce(empty); Require(empty.GetCount() == 0, "empty update region changed");
            auto one = Region({{3, 4, 9, 7}}); Coalesce(one);
            Require(Same(Rects(one), {{3, 4, 9, 7}}), "single update rectangle changed");
            auto slivers = Region({{450, 830, 1242, 882}, {450, 882, 463, 883}});
            const auto before = Rects(slivers);
            Coalesce(slivers, 0); Require(Same(Rects(slivers), before), "zero pass weight still merged");
        }
        // Pseudorandom unions: invariants at several weights, beyond the greedy limit too.
        uint32_t seed = 0x9e3779b9u;
        auto next = [&](uint32_t range) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % range; };
        for (int round = 0; round < 400; ++round) {
            tTVPComplexRect input;
            const int count = 1 + int(next(round < 360 ? 12 : 60));
            for (int i = 0; i < count; ++i) {
                const int x = int(next(1900)), y = int(next(1060));
                input.Or(tTVPRect(x, y, x + 1 + int(next(round % 3 ? 40 : 400)), y + 1 + int(next(round % 2 ? 6 : 80))));
            }
            for (uint64_t weight : {uint64_t(1), uint64_t(4096), DefaultPassPixels, uint64_t(1) << 22}) {
                tTVPComplexRect output; output.Or(input);
                Coalesce(output, weight);
                Invariants(input, output, weight);
            }
        }
    }
    // Production wiring: only an active Metal Layer session coalesces, and the
    // hint restores the original regions.
    for (const char* hint : {"1", "0"}) {
        SDL_SetHint("MIKAGE_METAL_LAYER_COALESCE_UPDATES", hint);
        TestPrimaryManager manager;
        manager.UpdateRegion.Or(tTVPRect(450, 830, 1242, 882));
        manager.UpdateRegion.Or(tTVPRect(450, 882, 463, 883));
        manager.NotifyUpdateRegionFixed();
        const bool merged = manager.UpdateRegion.GetCount() == 1;
        Require(merged == (metalBound && hint[0] == '1'), "update-region coalescing gate changed");
    }
    SDL_SetHint("MIKAGE_METAL_LAYER_COALESCE_UPDATES", "1");
    std::cout << "PASS update-region coalescing " << (metalBound ? "device fixtures/invariants/gate" : "software gate") << '\n';
}
