// Drives the production D3DAdaptor capture state machine against a pixel model
// of the backend. A canvas texture may only be handed to a layer once the
// script has shown that it clears the canvas after each capture; any later
// uncleared use must see exactly the frame it drew.
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using tjs_int = int;
using tjs_uint = unsigned;
using tjs_uint32 = std::uint32_t;
using uint64_t = std::uint64_t;
struct iTJSDispatch2;
class EmotePlayer;

namespace krkrsdl3 {
// Pixels are strings; "" is a cleared surface. Each write bumps a version.
struct iTVPRenderBackend {
    std::map<void*, std::string> pixels;
    std::map<void*, uint64_t> versions;
    void* current = nullptr;
    int exchanges = 0, restores = 0;
    void Write(void* h, std::string value) { pixels[h] = std::move(value); ++versions[h]; }
    void SetTarget(void* h) { current = h; }
    void ClearTarget(bool clear) { if (clear && current) Write(current, ""); }
    bool ExchangeTargetTextures(void* a, void* b) {
        if (!pixels.count(a) || !pixels.count(b) || a == b) return false;
        std::swap(pixels[a], pixels[b]); ++versions[a]; ++versions[b]; ++exchanges; return true;
    }
    bool GetTargetContentVersion(void* h, uint64_t& identity, uint64_t& version) {
        if (!pixels.count(h)) return false;
        identity = reinterpret_cast<uintptr_t>(h); version = versions[h]; return true;
    }
    bool CopyTargetToLayerTexture(void* source, void* destination) {
        if (!pixels.count(source) || !pixels.count(destination)) return false;
        Write(destination, pixels[source]); ++restores; return true;
    }
};
iTVPRenderBackend* backend = nullptr;
iTVPRenderBackend* TVPGetRenderBackend() { return backend; }
int directDraws = 0;
void TVPRecordEmoteLayerDirectDraw() { ++directDraws; }
namespace point_trace { struct WriterScope { explicit WriterScope(const char*) {} }; }
}

int restoreLost = 0;
void TVPConsoleLog(const char*, ...) { ++restoreLost; }

// The layer facade forwards to the production RenderMainImageOnGPU contract:
// the writer receives the layer's own handle and false means nothing changed.
struct tTJSNI_BaseLayer {
    void* handle;
    int width = 4, height = 4, updates = 0;
    tjs_uint GetWidth() const { return tjs_uint(width); }
    tjs_uint GetHeight() const { return tjs_uint(height); }
    bool RenderMainImageOnGPU(krkrsdl3::iTVPRenderBackend*, tjs_int w, tjs_int h,
                              const std::function<bool(void*)>& write) {
        return w == width && h == height && write(handle);
    }
    void Update() { ++updates; }
    // Records which frames the production code says the image consists of.
    std::function<bool(tjs_int, tjs_int)> hint;
    void SetMainImageTransparencyHint(std::function<bool(tjs_int, tjs_int)> h) { hint = std::move(h); }
};
int copies = 0;
static bool copyCanvasToLayer(tTJSNI_BaseLayer* layer, krkrsdl3::iTVPRenderBackend* renderer,
                              void* target, int, int, bool) {
    renderer->Write(layer->handle, renderer->pixels[target]); ++copies; layer->Update();
    return true;
}
struct EmoteDrawnFrame;
using HintFrames = std::vector<std::pair<std::shared_ptr<EmoteDrawnFrame>, uint64_t>>;
HintFrames lastHintFrames;
std::function<bool(tjs_int, tjs_int)> emoteTransparencyHint(HintFrames frames) {
    lastHintFrames = frames;
    return [](tjs_int, tjs_int) { return true; };
}

#define private public
#include "ProductionCaptureExchange.inc"
#undef private
// Construction allocates real backend targets in production; the rig assigns them.
D3DAdaptor::D3DAdaptor(iTJSDispatch2*, tjs_int width, tjs_int height, tjs_int, tjs_int)
    : _width(width), _height(height) {}
D3DAdaptor::~D3DAdaptor() {}

namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int handles[16];
struct Rig {
    krkrsdl3::iTVPRenderBackend renderer;
    D3DAdaptor adaptor{nullptr, 4, 4, 0, 0};
    tTJSNI_BaseLayer a{&handles[1]}, b{&handles[2]};
    Rig() {
        krkrsdl3::backend = &renderer;
        adaptor._target = &handles[0];
        renderer.Write(adaptor._target, ""); renderer.Write(a.handle, "old-a"); renderer.Write(b.handle, "old-b");
        copies = krkrsdl3::directDraws = restoreLost = 0;
    }
    std::string& canvas() { return renderer.pixels[adaptor._target]; }
    std::shared_ptr<EmoteDrawnFrame> player = std::make_shared<EmoteDrawnFrame>();
    std::shared_ptr<EmoteDrawnFrame> other = std::make_shared<EmoteDrawnFrame>();
    void draw(const std::string& frame, std::shared_ptr<EmoteDrawnFrame> who = nullptr) {
        if (!who) who = player;
        ++who->serial; adaptor.prepareForDraw(&renderer, who); renderer.Write(adaptor._target, canvas() + frame);
    }
    void capture(tTJSNI_BaseLayer& layer) { adaptor.captureInto(&layer, &renderer); }
    void cycle(const std::string& frame, tTJSNI_BaseLayer& layer) { draw(frame); capture(layer); adaptor.unloadUnusedTextures(); }
};

void exchangesOnlyAfterCleanCaptures() {
    Rig r;
    for (int i = 0; i < 3; ++i) r.cycle("f" + std::to_string(i), r.a);
    Require(copies == 3 && r.renderer.exchanges == 0, "exchanged before the clear pattern was seen");
    r.draw("f3"); r.capture(r.a);
    Require(copies == 3 && r.renderer.exchanges == 1 && krkrsdl3::directDraws == 1, "fourth capture copied");
    Require(r.renderer.pixels[r.a.handle] == "f3" && r.a.updates == 4, "layer did not receive the frame");
    r.adaptor.unloadUnusedTextures();
    Require(r.canvas().empty(), "clear did not reset the lent canvas");
    r.cycle("f4", r.b);
    Require(r.renderer.pixels[r.b.handle] == "f4" && r.renderer.exchanges == 2, "steady state did not exchange");
}
void unclearedDrawRestoresAndStops() {
    Rig r;
    for (int i = 0; i < 4; ++i) r.cycle("f" + std::to_string(i), r.a);
    r.draw("x"); r.capture(r.a);                    // exchanged
    r.draw("+y");                                    // draws on top without a clear
    Require(r.canvas() == "x+y" && restoreLost == 0, "uncleared draw lost the captured frame");
    r.capture(r.b);
    Require(r.renderer.pixels[r.b.handle] == "x+y" && r.renderer.pixels[r.a.handle] == "x", "accumulated frame wrong");
    const int exchanges = r.renderer.exchanges;
    r.adaptor.unloadUnusedTextures();
    for (int i = 0; i < 5; ++i) r.cycle("g", r.a);
    Require(r.renderer.exchanges == exchanges, "exchange resumed after an uncleared draw");
}
void secondCaptureGetsSameFrame() {
    Rig r;
    for (int i = 0; i < 4; ++i) r.cycle("f", r.a);
    r.draw("z"); r.capture(r.a); r.capture(r.b);
    Require(r.renderer.pixels[r.a.handle] == "z" && r.renderer.pixels[r.b.handle] == "z", "second capture differed");
}
void changedLayerCannotRestore() {
    Rig r;
    for (int i = 0; i < 4; ++i) r.cycle("f", r.a);
    r.draw("z"); r.capture(r.a);
    r.renderer.Write(r.a.handle, "script drew here");
    r.draw("+w");
    Require(restoreLost == 1 && r.canvas() == "+w", "restore from a modified layer was not reported");
}
// A capture's hint names exactly the frames drawn since the last clear.
void hintNamesFramesSinceClear() {
    Rig r;
    r.cycle("f", r.a);
    Require(r.a.hint && lastHintFrames.size() == 1 && lastHintFrames[0].first == r.player &&
            lastHintFrames[0].second == r.player->serial, "single-player capture hint wrong");
    r.draw("p"); r.draw("q", r.other); r.capture(r.b);
    Require(lastHintFrames.size() == 2 && lastHintFrames[1].first == r.other, "two-player capture hint wrong");
    r.adaptor.unloadUnusedTextures();
    r.capture(r.a);
    Require(lastHintFrames.empty() && r.a.hint, "cleared canvas capture should hint fully transparent");
    tTJSNI_BaseLayer wide{&handles[3]};
    wide.width = 8; r.renderer.Write(wide.handle, "");
    r.draw("w"); r.capture(wide);
    Require(!wide.hint, "partial copy registered a hint");
}
void sizeMismatchCopies() {
    Rig r;
    r.b.width = 8;
    for (int i = 0; i < 6; ++i) r.cycle("f", r.b);
    Require(r.renderer.exchanges == 0 && copies == 6, "mismatched layer was exchanged");
}
}

int main() {
    try {
        exchangesOnlyAfterCleanCaptures();
        unclearedDrawRestoresAndStops();
        secondCaptureGetsSameFrame();
        changedLayerCannotRestore();
        sizeMismatchCopies();
        hintNamesFramesSinceClear();
        std::cout << "PASS capture exchange: learned clear pattern, uncleared draw restore, repeat capture, "
                     "modified-layer fallback, size mismatch, transparency hints\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
