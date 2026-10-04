// Production runner/geometry coverage. Fixtures only construct decoded PSB
// resources and observe GPU command encoding; no runner code is extracted.
#include "emoterunner.h"
#include "emotegeometrybounds.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace emoteplayer;
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }
}

// Resource decoding and texture upload are outside this CPU suite. The fixture
// initializes the real production resource types without invoking PSB parsing.
namespace emoteplayer {
emotefile::emotefile() = default;
emotefile::~emotefile() = default;
emoteframe::emoteframe(emotefile* f, uint32_t) : _filePtr(f) { type = 2; }
emoteframe::~emoteframe() = default;
emotenode::emotenode(emotemotion* m, emotenode* p, std::vector<emotenode*>& list,
    emotefile* f, uint32_t) : _parent(p), _rootmotion(m), _filePtr(f) {
    list.push_back(this);
    if (p) p->children.push_back(this); else m->layer.push_back(this);
}
emotenode::~emotenode() = default;
emotemotion::emotemotion(emotefile* f, uint32_t) : parent(nullptr), lastTime(60), loopTime(-1), _filePtr(f) {}
emotemotion::~emotemotion() = default;
emoteobject::emoteobject(emotefile* f, uint32_t) : type(0), _filePtr(f) {}
emoteobject::~emoteobject() = default;
emotemetadata::emotemetadata(emotefile* f, uint32_t) : _filePtr(f) {}
emotemetadata::~emotemetadata() = default;
emoteicon::emoteicon(emotefile* f, uint32_t) : _filePtr(f) {}
emoteicon::~emoteicon() = default;
void emoteicon::ensureLoad() {}
emotesource::emotesource(emotefile* f, uint32_t) : type(0), _filePtr(f) {}
emotesource::~emotesource() = default;
emoteicon* emotefile::findsourceByName(const std::string& name) {
    for (auto& source : _source) {
        const auto icon = source.second->icon.find(name);
        if (icon != source.second->icon.end()) return icon->second;
    }
    return nullptr;
}
float emotefile::getZMax() { return 1; }
bool emotefile::getTickByName(const std::string& name, float& result) {
    if (!_metadata) return false;
    const auto found = _metadata->_varList.find(name);
    if (found == _metadata->_varList.end()) return false;
    result = found->second; return true;
}
void emotefile::setVariable(const std::string& name, tjs_real value) {
    if (_metadata) _metadata->_varList[name] = value;
}
emoteVar* emoteobject::findVarByName(const std::string&) { return nullptr; }
emotenode* emotemotion::getNodeByName(const std::string& label) {
    for (auto* node : nodeList) if (node->label == label) return node;
    return nullptr;
}
}
void TVPConsoleLog(const tjs_char*, ...) {}

namespace krkrsdl3 {
static iTVPRenderBackend* backend = nullptr;
iTVPRenderBackend* TVPGetRenderBackend() { return backend; }
void TVPRecordEmoteNodeProgress(uint64_t) {}
void TVPRecordEmoteSubmotionRebuild(uint64_t, uint64_t) {}
void TVPRecordEmoteShapeBuild(uint64_t, uint64_t) {}
void TVPRecordEmoteMeshBuild(uint64_t, uint64_t, uint64_t) {}
void TVPRecordEmoteMaskGroup(uint64_t) {}
void TVPRecordEmoteMaskClear() {}
void TVPRecordEmoteMaskDraw() {}
}

namespace {
class RecordingBackend : public krkrsdl3::iTVPRenderBackend {
public:
    bool gpu = false;
    int draws = 0;
    float opacity = 0;
    std::vector<EmoteVertex> vertices;
    const char* GetName() const override { return "fixture"; }
    void BeginFrame(int, int) override {}
    void EndFrame() override {}
    void* CreateWindowTexture(int, int) override { return this; }
    void UpdateWindowTexture(void*, const uint8_t*, int, int, int) override {}
    void DestroyWindowTexture(void*) override {}
    void DrawWindowTexture(void*, float, float, float, float) override {}
    void* CreateTarget(int, int) override { return this; }
    void DestroyTarget(void*) override {}
    void SetTarget(void*) override {}
    void ClearTarget(bool) override {}
    uint8_t* LockTarget(void*, int&) override { return nullptr; }
    void UnlockTarget(void*) override {}
    void* GetTargetTexture(void*) override { return this; }
    void UpdateTargetTexture(void*, const uint8_t*, int, int, int) override {}
    void* CreateTexture(int, int) override { return this; }
    void UpdateTexture(void*, const uint8_t*, int, int, int) override {}
    void DestroyTexture(void*) override {}
    void SetMask(void*) override {}
    void SetBlendMode(int, const float*) override {}
    void DrawMesh(const float* data, int count, const uint16_t*, int, void*, float opa, const float*) override {
        ++draws; opacity = opa; auto* v = reinterpret_cast<const EmoteVertex*>(data); vertices.assign(v, v + count);
    }
    bool SupportsMeshDeformation() const override { return gpu; }
    bool DrawDeformedMesh(int, int, const krkrsdl3::TVPMeshDeformSurface*, int, void*, float opa, const float*) override {
        ++draws; opacity = opa; return true;
    }
    void LayerSetBlend(int, float, const float*) override {}
    void LayerDrawRect(void*, float, float, float, float, float, float, float, float) override {}
};

struct Fixture {
    emotefile file;
    emotemetadata metadata{&file, 0};
    emoteobject object{&file, 0};
    emotemotion motion{&file, 0}, submotion{&file, 0};
    std::vector<std::unique_ptr<emotenode>> nodes;
    std::vector<std::unique_ptr<emoteframe>> frames;
    emoteengine engine;
    emotelimit limit{100, 100, 200, 200, 1, 200, 200};
    std::vector<emoteRender> root;
    Fixture() {
        file._metadata = &metadata; file._objects["chara"] = &object;
        object.motion["main"] = &motion; object.motion["sub"] = &submotion;
        motion.parent = submotion.parent = &object;
        engine._mainfile = &file; engine._mainmotion = &motion;
        emoteRender transform;
        transform.type = 3; transform.originX = transform.originY = 100;
        transform.width = transform.height = 200;
        transform.attachMat = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -1.0f, 1.0f);
        root.push_back(transform);
    }
    emotenode* node(const std::string& name, const std::string& source = "shape/rect", emotenode* parent = nullptr,
        emotemotion* owner = nullptr) {
        if (!owner) owner = &motion;
        nodes.emplace_back(std::make_unique<emotenode>(owner, parent, owner->nodeList, &file, 0));
        auto* node = nodes.back().get(); node->label = name;
        frames.emplace_back(std::make_unique<emoteframe>(&file, 0));
        auto* frame = frames.back().get(); frame->hasContent = true; frame->src = source;
        node->frameList.push_back(frame);
        return node;
    }
    emoteframe* next(emotenode* node, double time, double x) {
        frames.emplace_back(std::make_unique<emoteframe>(&file, 0));
        auto* frame = frames.back().get(); *frame = *node->frameList.front();
        frame->time = time; frame->coordX = x; node->frameList.push_back(frame); return frame;
    }
    void step(float tick) { engine.progress(tick, root, limit); }
    emotenoderef* ref(emotenode* node) { return engine._mainMotionRef->getNodeRef(node); }
};

void testStaticAndDependencies() {
    SDL_SetHint("MIKAGE_EMOTE_NODE_CACHE", "0");
    SDL_SetHint("MIKAGE_EMOTE_LOCAL_POSE_CACHE", "0");
    Fixture f; auto* shape = f.node("shape");
    f.step(0); check(!f.engine.nodeCachingEnabled(), "node cache defaults off");
    auto generation = f.engine.poseRevision(); f.step(1);
    check(f.engine.poseRevision() > generation && !f.engine.contentTrackingEnabled(), "all flags off uses cheap conservative content revision");
    SDL_SetHint("MIKAGE_EMOTE_CAPTURE_CACHE", "1"); f.step(1); generation = f.engine.poseRevision(); f.step(1);
    check(f.engine.poseRevision() == generation, "capture-only content revision remains exact without diagnostics");
    SDL_SetHint("MIKAGE_EMOTE_CAPTURE_CACHE", "0");
    SDL_SetHint("MIKAGE_EMOTE_NODE_CACHE", "1"); resetPerformanceStats();
    f.step(1); check(!f.engine.localPoseCachingEnabled(), "local pose experiment defaults off independently");
    SDL_SetHint("MIKAGE_EMOTE_LOCAL_POSE_CACHE", "1");
    f.step(2); generation = f.engine.poseRevision();
    const auto original = f.ref(shape)->_shapeArea.vertices;
    for (int i = 3; i < 20; ++i) f.step(float(i));
    auto stats = performanceStats();
    check(stats.nodeCacheHits >= 18 && stats.shapeCacheHits >= 18, "stationary geometry reused");
    check(stats.localPoseCacheHits >= 17, "stationary local pose reused separately");
    check(f.engine.poseRevision() == generation, "stationary revision stable");
    f.root.front().attachMat = glm::translate(f.root.front().attachMat, glm::vec3(10, 0, 0));
    f.step(20); check(f.engine.poseRevision() > generation, "root transform invalidates");
    check(!near(original[0].x, f.ref(shape)->_shapeArea.vertices[0].x), "root transform changes vertices");
    generation = f.engine.poseRevision(); f.limit.viewW = 400; f.step(21);
    check(f.engine.poseRevision() > generation, "viewport invalidates shape script bounds");
    generation = f.engine.poseRevision(); shape->removed = true; f.step(22);
    check(f.engine.poseRevision() > generation && f.engine._mainMotionRef->shapeNodeAreas.empty(), "hide invalidates and removes shape");
    shape->removed = false; f.step(23); check(f.engine._mainMotionRef->shapeNodeAreas.size() == 1, "shape resurfaces");
    generation = f.engine.poseRevision(); f.engine._mainmotion = nullptr; f.step(24);
    check(f.engine.poseRevision() > generation && !f.engine._mainMotionRef, "unload invalidates");
    f.engine._mainmotion = &f.motion; f.step(25); check(bool(f.engine._mainMotionRef), "recreate after unload");
}

void testParentAndSubmotion() {
    Fixture f; auto* parent = f.node("parent", "layout");
    auto* child = f.node("child", "shape/rect", parent);
    f.step(0); auto old = f.ref(child)->_shapeArea.vertices; auto revision = f.engine.poseRevision();
    parent->frameList[0]->coordX = 7; f.step(0);
    check(f.engine.poseRevision() > revision, "parent pose invalidates descendants");
    check(!near(old[0].x, f.ref(child)->_shapeArea.vertices[0].x), "child geometry follows parent");
    parent->frameList[0]->angle = 20; parent->frameList[0]->zx = -1; f.step(0);
    check(!near(old[1].x, f.ref(child)->_shapeArea.vertices[1].x), "rotation and mirror invalidate");
    auto* instance = f.node("instance", "motion/chara/sub");
    auto* sub = f.node("animated", "shape/rect", nullptr, &f.submotion); f.next(sub, 60, 30);
    f.step(0); auto* subref = f.ref(instance)->currentMtnRef;
    check(subref && subref->shapeNodeAreas.size() == 1, "submotion shape collected");
    old = subref->shapeNodeAreas.front().vertices; revision = f.engine.poseRevision();
    f.step(10); check(f.ref(instance)->currentMtnRef == subref, "submotion instance pooled");
    check(f.engine.poseRevision() > revision && !near(old[0].x, subref->shapeNodeAreas.front().vertices[0].x), "submotion clock advances under static parent");
    revision = f.engine.poseRevision(); instance->frameList[0]->timeOffset = 20; f.step(10);
    check(f.engine.poseRevision() > revision, "submotion time offset invalidates");
    instance->frameList[0]->src = "layout"; f.step(10);
    check(f.engine._mainMotionRef->_subMotionRefs.empty(), "submotion removed on selection change");
    instance->frameList[0]->src = "motion/chara/sub"; f.step(10);
    check(f.ref(instance)->currentMtnRef && f.ref(instance)->currentMtnRef->shapeNodeAreas.size() == 1, "submotion restored without stale geometry");
    f.node("new shape"); f.step(10);
    check(f.engine._mainMotionRef->_nodeIndex.size() == f.motion.nodeList.size(), "topology rebuild refreshes node index");
}

void testVariablesCloneAndRestore() {
    Fixture f; f.engine.inheritAnimationMode(true); f.metadata._varList["pose"] = 0;
    emoteVar parameter; parameter.id = "pose"; parameter.division = 60;
    f.motion.parameter.push_back(&parameter);
    auto* shape = f.node("shape"); shape->isParameterize = true; shape->parameterIdx = 0; f.next(shape, 60, 40);
    f.step(0); const auto old = f.ref(shape)->_shapeArea.vertices;
    f.engine.setAnimationVariable("pose", 1, 30, 0); f.engine.advanceAnimation(250, 20); f.step(float(f.engine._animationClock));
    check(near(float(f.engine._animationClock), 15) && f.engine.getVariable("pose") > 0, "variable transition and clock advance while cache enabled");
    check(!near(old[0].x, f.ref(shape)->_shapeArea.vertices[0].x), "variable changes prepared geometry");
    emoteengine clone; clone._mainfile = &f.file; clone._mainmotion = &f.motion; clone.inheritAnimationMode(true); clone.copyAnimationStateFrom(f.engine);
    clone.progress(float(clone._animationClock), f.root, f.limit);
    check(clone._mainMotionRef.get() != f.engine._mainMotionRef.get(), "clone owns runtime nodes");
    const auto cloned = clone._mainMotionRef->getNodeRef(shape)->_shapeArea.vertices;
    check(near(cloned[0].x, f.ref(shape)->_shapeArea.vertices[0].x), "clone pose matches source");
    clone.setAnimationVariable("pose", 0, 0, 0); clone.progress(float(clone._animationClock), f.root, f.limit);
    check(!near(clone._mainMotionRef->getNodeRef(shape)->_shapeArea.vertices[0].x, f.ref(shape)->_shapeArea.vertices[0].x), "shared resource players stay isolated");
    const auto saved = f.engine.serializeAnimationState(); const auto savedX = f.ref(shape)->_shapeArea.vertices[0].x;
    f.engine.advanceAnimation(250, 20); f.step(float(f.engine._animationClock));
    check(f.engine.restoreAnimationState(saved), "restore succeeds"); f.step(float(f.engine._animationClock));
    check(near(savedX, f.ref(shape)->_shapeArea.vertices[0].x), "restored state invalidates and reconstructs pose");
    check(!f.engine.restoreAnimationState("corrupt"), "invalid restore rejected");
    eyeControl eye; eye.label = "pose"; eye.beginFrame = 0; eye.endFrame = 1;
    eye.blinkFrameCount = 4; eye.blinkIntervalMin = eye.blinkIntervalMax = 2;
    eye.uid = std::uniform_int_distribution<int32_t>(2, 2);
    f.metadata._eyeControl.push_back(&eye); f.engine.resetAnimationState(); f.step(0);
    f.engine.advanceAnimation(16.666666666666668, 20); f.step(float(f.engine._animationClock));
    f.engine.advanceAnimation(50, 20); f.step(float(f.engine._animationClock));
    check(f.engine._animationEyes.front().isBlinking, "automatic blink timer advances while stationary");
    f.engine.advanceAnimation(16.666666666666668, 20); f.step(float(f.engine._animationClock));
    check(f.engine.getVariable("pose") > 0, "automatic blink changes pose");
}

void testSharedSentinelsAndLoop() {
    Fixture f; auto* shape = f.node("sentinel");
    auto* first = shape->frameList.front();
    first->coordX = std::numeric_limits<double>::quiet_NaN();
    first->coordY = std::numeric_limits<double>::infinity();
    f.step(0); check(f.ref(shape)->currCoordx == -100 && f.ref(shape)->currCoordy == 100, "sentinels resolved for first viewport");
    emoteengine second; second._mainfile = &f.file; second._mainmotion = &f.motion;
    auto secondLimit = f.limit; secondLimit.originX = 40; secondLimit.originY = 20; secondLimit.height = 300;
    second.progress(0, f.root, secondLimit);
    check(second._mainMotionRef->getNodeRef(shape)->currCoordx == -40 &&
          second._mainMotionRef->getNodeRef(shape)->currCoordy == 280, "shared frame sentinels resolve independently");
    check(std::isnan(first->coordX) && std::isinf(first->coordY), "decoded frames remain immutable");
    auto* next = f.next(shape, 60, 10); next->coordY = 0;
    f.step(30); const float a = f.ref(shape)->currCoordx;
    second.progress(30, f.root, secondLimit);
    check(near(a, -45) && near(second._mainMotionRef->getNodeRef(shape)->currCoordx, -15), "interpolated sentinels isolate viewports");
    f.motion.loopTime = 60; f.step(90); check(near(a, f.ref(shape)->currCoordx), "motion loop still advances and wraps");
    f.step(5); auto generation = f.engine.poseRevision(); f.step(5);
    check(generation == f.engine.poseRevision(), "repeated animated tick yields unchanged pose");
}

void testIconGPUAndBounds() {
    Fixture f; RecordingBackend renderer; krkrsdl3::backend = &renderer;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    auto* node = f.node("icon", "icon"); f.step(0);
    auto* ref = f.ref(node); check(ref->_meshVertices.size() == 4 && !ref->_useGPUDeform, "production CPU icon rect prepared");
    f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(renderer.draws == 1 && ref->wasDrawn, "production draw submits icon");
    const auto bounds = performance::submittedCPUBounds(f.engine._mainMotionRef->drawNodes(), 200, 200);
    check(bounds.known && !bounds.rect.empty(), "CPU bounds instantiated with real runner nodes");
    auto revision = f.engine.poseRevision(); resetPerformanceStats(); f.step(1);
    check(performanceStats().meshCacheHits == 1 && f.engine.poseRevision() == revision, "stationary icon mesh reused");
    node->frameList.front()->hasbp = true; f.step(2);
    check(f.ref(node)->_meshVertices.size() == 81, "CPU deformation subdivides mesh");
    renderer.gpu = true; f.step(3); check(ref->_useGPUDeform && ref->_meshVertices.empty(), "backend change prepares GPU surface chain");
    f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(!performance::submittedCPUBounds(f.engine._mainMotionRef->drawNodes(), 200, 200).known, "GPU deform excluded from production bounds");
    check(performance::submittedExperimentalBounds(f.engine._mainMotionRef->drawNodes(), 200, 200).known,
        "experimental GPU bounds instantiated with production nodes");
    auto surfaces = ref->_gpuDeformSurfaces; revision = f.engine.poseRevision(); f.step(4);
    check(f.engine.poseRevision() == revision && ref->_gpuDeformSurfaces.size() == surfaces.size(), "stationary GPU surfaces reused");
    node->frameList.front()->bp[12] += .1; f.step(5);
    check(f.engine.poseRevision() > revision, "Bezier control-point edit invalidates");
    revision = f.engine.poseRevision(); node->meshDivision = 12; f.step(6);
    check(f.engine.poseRevision() > revision && ref->_meshDivX == 12, "mesh division invalidates");
    revision = f.engine.poseRevision(); node->frameList.front()->color = 0x80ffffff; node->frameList.front()->hasColor = true; f.step(7);
    check(f.engine.poseRevision() > revision, "frame color invalidates drawn content");
    revision = f.engine.poseRevision(); icon.selftexture = &source; f.step(8);
    check(f.engine.poseRevision() > revision, "texture replacement invalidates content");
    node->frameList.front()->opa = 0; f.step(9); f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(!ref->wasDrawn, "zero opacity is not submitted");
    renderer.gpu = false; node->frameList.front()->opa = 1; f.step(10);
    check(!ref->_useGPUDeform && ref->_meshVertices.size() == 169, "GPU capability removal rebuilds exact CPU mesh");
    krkrsdl3::backend = nullptr;
}

void testDrawOrder() {
    Fixture f; auto* a = f.node("a"); auto* b = f.node("b");
    a->frameList.front()->coordZ = .1; b->frameList.front()->coordZ = .2;
    f.step(0); check(f.engine._mainMotionRef->drawNodes().front()->currentNode == b, "depth sorting uses production draw list");
    resetPerformanceStats(); f.step(1); check(performanceStats().drawListRebuilds == 0, "unchanged depth list retained");
    b->frameList.front()->coordZ = -.1; f.step(2);
    check(f.engine._mainMotionRef->drawNodes().front()->currentNode == a, "changed depth rebuilds ordering");
}

void testCacheParityAndEarlySeek() {
    SDL_SetHint("MIKAGE_EMOTE_CAPTURE_CACHE", "1");
    Fixture f; auto* parent = f.node("parent", "layout"); auto* shape = f.node("shape", "shape/circle", parent);
    f.next(parent, 60, 15); f.next(shape, 60, -10);
    for (float tick : {0.f, 1.f, 10.f, 30.f, 59.f, 60.f, 70.f, 2.f, 2.f}) {
        SDL_SetHint("MIKAGE_EMOTE_NODE_CACHE", "0"); f.step(tick);
        const auto exact = f.ref(shape)->_shapeArea.vertices; const auto revision = f.engine.poseRevision();
        SDL_SetHint("MIKAGE_EMOTE_NODE_CACHE", "1"); f.step(tick);
        const auto& cached = f.ref(shape)->_shapeArea.vertices;
        check(cached.size() == exact.size(), "cache parity vertex count");
        bool equal = true;
        for (size_t i = 0; i < exact.size(); ++i)
            equal &= cached[i].x == exact[i].x && cached[i].y == exact[i].y && cached[i].u == exact[i].u && cached[i].v == exact[i].v;
        check(equal && revision == f.engine.poseRevision(), "enabled cache produces exact baseline geometry");
    }
    // A warm node can seek before its first keyframe. It must not dereference
    // a null frame left by checkDrawStatus or retain an earlier visible shape.
    shape->frameList[0]->time = 10; f.step(20); f.step(0);
    check(f.ref(shape)->frame == nullptr && f.engine._mainMotionRef->shapeNodeAreas.empty(), "early seek invalidates warm node safely");
    SDL_SetHint("MIKAGE_EMOTE_CAPTURE_CACHE", "0");
}

struct BenchResult { double ms; std::uint64_t hits, visits, localHits; };
BenchResult benchmark(bool enabled, bool local, int animatedEvery, int players) {
    SDL_SetHint("MIKAGE_EMOTE_NODE_CACHE", enabled ? "1" : "0");
    SDL_SetHint("MIKAGE_EMOTE_LOCAL_POSE_CACHE", local ? "1" : "0");
    std::vector<std::unique_ptr<Fixture>> instances;
    for (int p = 0; p < players; ++p) {
        instances.push_back(std::make_unique<Fixture>());
        for (int i = 0; i < 160; ++i) {
            auto* node = instances.back()->node("node" + std::to_string(i), i % 3 == 0 ? "shape/circle" : "shape/rect");
            if (animatedEvery && i % animatedEvery == 0) instances.back()->next(node, 60, 10);
        }
        instances.back()->step(0);
    }
    resetPerformanceStats();
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 1200; ++frame)
        for (auto& instance : instances) instance->step(float(frame % 60));
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto stats = performanceStats(); return {elapsed, stats.nodeCacheHits, stats.nodeVisits, stats.localPoseCacheHits};
}
void runBenchmarks() {
    for (int players : {1, 2}) for (int animatedEvery : {0, 4, 1}) {
        const auto off = benchmark(false, false, animatedEvery, players), on = benchmark(true, false, animatedEvery, players),
            local = benchmark(true, true, animatedEvery, players);
        std::cout << "BENCH players=" << players << " animated=" << (animatedEvery ? 100 / animatedEvery : 0)
            << "% baseline_ms=" << off.ms << " cache_ms=" << on.ms << " reduction=" << (off.ms - on.ms) * 100 / off.ms
            << "% local_experiment_ms=" << local.ms << " local_hits=" << local.localHits
            << " hits=" << on.hits << '/' << on.visits << '\n';
    }
}
}
int main(int argc, char**) {
    try {
        testStaticAndDependencies(); testParentAndSubmotion(); testVariablesCloneAndRestore();
        testSharedSentinelsAndLoop(); testIconGPUAndBounds(); testDrawOrder(); testCacheParityAndEarlySeek();
        std::cout << "Production node/geometry: " << checks << " checks passed\n";
        if (argc > 1) runBenchmarks();
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
