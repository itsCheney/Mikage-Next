// Production runner/geometry coverage. Fixtures only construct decoded PSB
// resources and observe GPU command encoding; no runner code is extracted.
#include "emoterunner.h"
#include "emotephysics.h"
#include <set>
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

// emotefile's TJS string tables stay empty here; the VM is not linked, so
// their element destructors only need to exist.
TJS::tTJSVariant::~tTJSVariant() {}
void TJS::tTJSVariantString::Release() {}

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
bustControl::~bustControl() = default;
uniControl::~uniControl() = default;
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
    std::vector<std::pair<char, void*>> events; // 'T' target, 'C' clear, 'M' mask, 'D' draw
    uintptr_t nextTarget = 0x1000;
    const char* GetName() const override { return "fixture"; }
    void BeginFrame(int, int) override {}
    void EndFrame() override {}
    void* CreateWindowTexture(int, int) override { return this; }
    void UpdateWindowTexture(void*, const uint8_t*, int, int, int) override {}
    void DestroyWindowTexture(void*) override {}
    void DrawWindowTexture(void*, float, float, float, float) override {}
    void* CreateTarget(int, int) override { nextTarget += 0x100; return reinterpret_cast<void*>(nextTarget); }
    void DestroyTarget(void*) override {}
    void SetTarget(void* target) override { events.emplace_back('T', target); }
    void ClearTarget(bool) override { events.emplace_back('C', nullptr); }
    uint8_t* LockTarget(void*, int&) override { return nullptr; }
    void UnlockTarget(void*) override {}
    void* GetTargetTexture(void*) override { return this; }
    void UpdateTargetTexture(void*, const uint8_t*, int, int, int) override {}
    void* CreateTexture(int, int) override { return this; }
    void UpdateTexture(void*, const uint8_t*, int, int, int) override {}
    void DestroyTexture(void*) override {}
    void SetMask(void* mask) override { events.emplace_back('M', mask); }
    void SetBlendMode(int, const float*) override {}
    void DrawMesh(const float* data, int count, const uint16_t*, int, void*, float opa, const float*) override {
        ++draws; opacity = opa; events.emplace_back('D', nullptr); auto* v = reinterpret_cast<const EmoteVertex*>(data); vertices.assign(v, v + count);
    }
    bool SupportsMeshDeformation() const override { return gpu; }
    bool DrawDeformedMesh(int, int, const krkrsdl3::TVPMeshDeformSurface*, int, void*, float opa, const float*) override {
        ++draws; opacity = opa; events.emplace_back('D', nullptr); return true;
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

// Shape hit areas keep the frame's surface chain; their mesh is built on use.
const emoterect& shapeArea(const emotemotionref& motion, const std::string& label) {
    for (const auto& area : motion.shapeNodeAreas)
        if (area.label == label) return area.geometry();
    static const emoterect none;
    return none;
}

void testLazyShapeGeometry() {
    Fixture f; auto* circle = f.node("circle", "shape/circle");
    f.step(0);
    const auto& recorded = f.engine._mainMotionRef->shapeNodeAreas;
    check(recorded.size() == 1 && !recorded.front().built, "recording a shape builds no mesh");
    // Centre of the 200x200 view: inside the unit circle mapped to clip space.
    check(recorded.front().contains(0.0f, 0.0f), "circle contains its centre");
    check(recorded.front().built && recorded.front().vertices.size() == 65, "first query builds the circle");
    check(!recorded.front().contains(0.99f, 0.99f), "circle excludes the corner");
    std::vector<EmoteVertex> eager; std::vector<uint16_t> eagerIndices;
    buildShapeMesh(recorded.front().methods, 1, eager, eagerIndices);
    check(eager.size() == recorded.front().vertices.size() &&
          near(eager[7].x, recorded.front().vertices[7].x) &&
          near(eager[7].y, recorded.front().vertices[7].y), "lazy mesh equals the eager build");
    f.step(1);
    check(!f.engine._mainMotionRef->shapeNodeAreas.front().built, "next frame drops the stale mesh");
    (void)circle;
}

// Masks are drawn before the canvas, so the canvas is drawn without a target switch.
void testMaskPrepass() {
    RecordingBackend renderer; krkrsdl3::backend = &renderer; // outlives the engine's mask pool
    Fixture f;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    std::vector<emotenode*> bodies;
    for (int i = 0; i < 7; ++i) bodies.push_back(f.node("body" + std::to_string(i), "icon"));
    auto* stencil = f.node("stencil", "icon");
    void* canvas = reinterpret_cast<void*>(0x10);
    void* playerMask = reinterpret_cast<void*>(0x20);
    auto run = [&](size_t maskedCount, bool prepare = true) {
        f.step(0);
        for (size_t i = 0; i < bodies.size(); ++i) {
            auto& method = f.ref(bodies[i])->renderMethod.front();
            method.hasStencil = i < maskedCount;
            method.layerNode.assign(i < maskedCount ? 1 : 0, f.ref(stencil));
        }
        renderer.events.clear();
        if (prepare) f.engine.prepareMasks(&renderer, f.limit, playerMask);
        renderer.SetTarget(canvas); renderer.ClearTarget(true);
        const size_t canvasStart = renderer.events.size() - 2;
        f.engine.draw(&renderer, canvas, f.limit, playerMask);
        size_t switches = 0, maskedDraws = 0;
        std::set<void*> masks;
        for (size_t i = canvasStart; i < renderer.events.size(); ++i) {
            const auto& [kind, target] = renderer.events[i];
            if (kind == 'T' && target != canvas) ++switches;
            if (kind == 'M' && target) { ++maskedDraws; masks.insert(target); }
        }
        return std::tuple<size_t, size_t, size_t>{switches, maskedDraws, masks.size()};
    };
    auto [switches, maskedDraws, distinct] = run(3);
    check(switches == 0, "prepared masks leave the canvas pass unbroken");
    check(maskedDraws == 3 && distinct == 3, "each masked node samples its own mask");
    check(f.ref(bodies[0])->wasDrawn && f.ref(bodies[6])->wasDrawn, "prepass keeps every node drawn");
    std::tie(switches, maskedDraws, distinct) = run(5);
    check(switches == 0 && maskedDraws == 5 && distinct == 5, "player mask plus pool covers five masks");
    std::tie(switches, maskedDraws, distinct) = run(6); // mask set + stencil draw per node
    check(switches == 12 && maskedDraws == 6 && distinct == 1, "more masks than targets draw in place");
    std::tie(switches, maskedDraws, distinct) = run(3, false);
    check(switches == 6 && maskedDraws == 3 && distinct == 3, "draw without a prepass still uses separate masks");
    f.engine.releaseMaskTargets(); krkrsdl3::backend = nullptr;
}

// Nodes share one parent chain: each must hand it back unchanged.
void testSharedChain() {
    Fixture f; RecordingBackend renderer; krkrsdl3::backend = &renderer;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    auto* deep = f.node("deep", "layout"); deep->frameList.front()->coordZ = .5;
    auto* child = f.node("child", "icon", deep);
    auto* masked = f.node("masked", "layout"); masked->type = 12;
    masked->stencilCompositeMaskLayerList = {"stencil"};
    auto* maskedChild = f.node("maskedChild", "icon", masked);
    auto* stencil = f.node("stencil", "icon");
    auto* sibling = f.node("sibling", "icon");
    const auto root = f.root.front().attachMat;
    for (int i = 0; i < 2; ++i) {
        f.step(float(i));
        check(f.root.size() == 1 && f.root.front().attachMat == root && !f.root.front().hasStencil &&
              f.root.front().layerNode.empty(), "caller's chain is handed back unchanged");
        check(f.ref(child)->renderMethod.size() == 3 && !near(f.ref(child)->getCurrentRenderZ(), 0),
              "child inherits its parent's depth");
        check(near(f.ref(sibling)->getCurrentRenderZ(), 0) && f.ref(sibling)->renderMethod.size() == 2,
              "sibling does not inherit another branch's depth");
        const auto& maskedMethod = f.ref(maskedChild)->renderMethod.front();
        check(maskedMethod.hasStencil && maskedMethod.layerNode.size() == 1 &&
              maskedMethod.layerNode.front() == f.ref(stencil), "type 12 mask reaches its children");
        check(!f.ref(sibling)->needsMask() && !f.ref(stencil)->needsMask(), "mask stays in its branch");
        check(f.ref(deep)->renderMethod.empty(), "layouts keep no chain copy");
    }
    krkrsdl3::backend = nullptr;
}

// Physics samples reuse one tree and evaluate only branches with an anchor.
void testPhysicsSample() {
    Fixture f;
    auto* body = f.node("body", "layout");
    auto* anchor = f.node("anchor", "layout", body);
    auto* other = f.node("other", "layout");
    auto* otherChild = f.node("otherChild", "layout", other);
    bustControl rule; rule.baseLayer = "anchor";
    f.engine.physics.reset({&rule}, {}, {});
    f.engine.samplePhysics(0, f.root.front(), f.limit);
    auto* sample = f.engine.physicsSample();
    check(sample && sample->getNodeRef(anchor)->frame && sample->getNodeRef(body)->frame,
          "anchor and its parent are sampled");
    check(!sample->getNodeRef(other)->frame && !sample->getNodeRef(otherChild)->frame,
          "branches without an anchor are skipped");
    f.engine.samplePhysics(1, f.root.front(), f.limit);
    check(f.engine.physicsSample() == sample, "sample tree is reused");
    bustControl moved; moved.baseLayer = "otherChild";
    f.engine.physics.reset({&moved}, {}, {});
    f.engine.samplePhysics(2, f.root.front(), f.limit);
    check(sample->getNodeRef(otherChild)->frame, "a new anchor set is honoured");
}

void testStaticAndDependencies() {
    Fixture f; auto* shape = f.node("shape");
    f.step(0); const auto original = shapeArea(*f.engine._mainMotionRef, shape->label).vertices;
    for (int i = 1; i < 20; ++i) f.step(float(i));
    check(near(original[0].x, shapeArea(*f.engine._mainMotionRef, shape->label).vertices[0].x), "stationary shape keeps its geometry");
    f.root.front().attachMat = glm::translate(f.root.front().attachMat, glm::vec3(10, 0, 0));
    f.step(20); check(!near(original[0].x, shapeArea(*f.engine._mainMotionRef, shape->label).vertices[0].x), "root transform changes vertices");
    const float left = shapeArea(*f.engine._mainMotionRef, shape->label).left; f.limit.viewW = 400; f.step(21);
    check(!near(left, shapeArea(*f.engine._mainMotionRef, shape->label).left), "viewport changes shape script bounds");
    shape->removed = true; f.step(22);
    check(f.engine._mainMotionRef->shapeNodeAreas.empty(), "hidden shape removed");
    shape->removed = false; f.step(23); check(f.engine._mainMotionRef->shapeNodeAreas.size() == 1, "shape resurfaces");
    f.engine._mainmotion = nullptr; f.step(24);
    check(!f.engine._mainMotionRef, "unload releases the runtime tree");
    f.engine._mainmotion = &f.motion; f.step(25); check(bool(f.engine._mainMotionRef), "recreate after unload");
}

void testParentAndSubmotion() {
    Fixture f; auto* parent = f.node("parent", "layout");
    auto* child = f.node("child", "shape/rect", parent);
    f.step(0); auto old = shapeArea(*f.engine._mainMotionRef, child->label).vertices;
    parent->frameList[0]->coordX = 7; f.step(0);
    check(!near(old[0].x, shapeArea(*f.engine._mainMotionRef, child->label).vertices[0].x), "child geometry follows parent");
    parent->frameList[0]->angle = 20; parent->frameList[0]->zx = -1; f.step(0);
    check(!near(old[1].x, shapeArea(*f.engine._mainMotionRef, child->label).vertices[1].x), "rotation and mirror propagate");
    auto* instance = f.node("instance", "motion/chara/sub");
    auto* sub = f.node("animated", "shape/rect", nullptr, &f.submotion); f.next(sub, 60, 30);
    f.step(0); auto* subref = f.ref(instance)->currentMtnRef;
    check(subref && subref->shapeNodeAreas.size() == 1, "submotion shape collected");
    old = subref->shapeNodeAreas.front().geometry().vertices;
    f.step(10); check(f.ref(instance)->currentMtnRef == subref, "submotion instance pooled");
    check(!near(old[0].x, subref->shapeNodeAreas.front().geometry().vertices[0].x), "submotion clock advances under static parent");
    old = subref->shapeNodeAreas.front().geometry().vertices; instance->frameList[0]->timeOffset = 20; f.step(10);
    check(!near(old[0].x, subref->shapeNodeAreas.front().geometry().vertices[0].x), "submotion time offset moves its pose");
    instance->frameList[0]->src = "layout"; f.step(10);
    check(f.engine._mainMotionRef->_subMotionRefs.empty(), "submotion removed on selection change");
    instance->frameList[0]->src = "motion/chara/sub"; f.step(10);
    check(f.ref(instance)->currentMtnRef && f.ref(instance)->currentMtnRef->shapeNodeAreas.size() == 1, "submotion restored without stale geometry");
    f.node("new shape"); f.step(10);
    check(f.engine._mainMotionRef->_nodeIndex.size() == f.motion.nodeList.size(), "topology rebuild refreshes node index");
}

void testVariablesAndIsolation() {
    Fixture f; f.metadata._varList["pose"] = 0;
    emoteVar parameter; parameter.id = "pose"; parameter.division = 60;
    f.motion.parameter.push_back(&parameter);
    auto* shape = f.node("shape"); shape->isParameterize = true; shape->parameterIdx = 0; f.next(shape, 60, 40);
    f.engine.resetControls(); f.step(0); const auto old = shapeArea(*f.engine._mainMotionRef, shape->label).vertices;
    auto advance = [&](emoteengine& engine, double tick) {
        engine.controlTick = tick; engine.updateTimelineControl(tick, true); engine.updateEyeControl(tick, true);
    };
    f.engine.setVariable("pose", 1, 30, 0); advance(f.engine, 15); f.step(15);
    check(near(float(f.engine.getVariable("pose")), 0.5f), "variable transition reaches half at half time");
    check(!near(old[0].x, shapeArea(*f.engine._mainMotionRef, shape->label).vertices[0].x), "variable changes prepared geometry");
    emoteengine second; second._mainfile = &f.file; second._mainmotion = &f.motion; second.resetControls();
    advance(second, 15); second.progress(15, f.root, f.limit);
    check(second._mainMotionRef.get() != f.engine._mainMotionRef.get(), "players own runtime nodes");
    check(near(shapeArea(*second._mainMotionRef, shape->label).vertices[0].x, old[0].x), "shared resource players stay isolated");
    check(near(float(f.metadata._varList["pose"]), 0), "player writes leave shared metadata unchanged");
    eyeControl eye; eye.label = "pose"; eye.beginFrame = 0; eye.endFrame = 1;
    eye.blinkFrameCount = 4; eye.blinkIntervalMin = eye.blinkIntervalMax = 2;
    eye.uid = std::uniform_int_distribution<int32_t>(2, 2);
    f.metadata._eyeControl.push_back(&eye); f.engine.resetControls();
    advance(f.engine, 0); advance(f.engine, 3); advance(f.engine, 4);
    check(f.engine.getVariable("pose") == 0, "blink does not change the base value");
    float blinked = 0;
    check(f.engine.getControlVariable("pose", blinked) && blinked > 0, "automatic blink changes pose");
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
    f.step(5); const float repeated = f.ref(shape)->currCoordx; f.step(5);
    check(near(repeated, f.ref(shape)->currCoordx), "repeated animated tick yields unchanged pose");
}

void testIconAndGPU() {
    Fixture f; RecordingBackend renderer; krkrsdl3::backend = &renderer;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    auto* node = f.node("icon", "icon"); f.step(0);
    auto* ref = f.ref(node); check(ref->_meshVertices.size() == 4 && !ref->_useGPUDeform, "production CPU icon rect prepared");
    f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(renderer.draws == 1 && ref->wasDrawn, "production draw submits icon");
    node->frameList.front()->hasbp = true; f.step(2);
    check(f.ref(node)->_meshVertices.size() == 81, "CPU deformation subdivides mesh");
    renderer.gpu = true; f.step(3); check(ref->_useGPUDeform && ref->_meshVertices.empty(), "backend change prepares GPU surface chain");
    f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    const auto surfaces = ref->_gpuDeformSurfaces; f.step(4);
    check(ref->_gpuDeformSurfaces.size() == surfaces.size(), "stationary GPU surface chain stable");
    node->frameList.front()->bp[12] += .1; f.step(5);
    bool controlChanged = false;
    for (size_t i = 0; i < surfaces.size() && i < ref->_gpuDeformSurfaces.size(); ++i)
        for (size_t c = 0; c < std::size(surfaces[i].controlPts); ++c)
            controlChanged |= surfaces[i].controlPts[c] != ref->_gpuDeformSurfaces[i].controlPts[c];
    check(controlChanged, "Bezier control-point edit reaches the GPU surface chain");
    node->meshDivision = 12; f.step(6);
    check(ref->_meshDivX == 12, "mesh division applies");
    node->frameList.front()->opa = 0; f.step(9); f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(!ref->wasDrawn, "zero opacity is not submitted");
    renderer.gpu = false; node->frameList.front()->opa = 1; f.step(10);
    check(!ref->_useGPUDeform && ref->_meshVertices.size() == 169, "GPU capability removal rebuilds exact CPU mesh");
    krkrsdl3::backend = nullptr;
}

// E-mote characters nest icons far deeper than 16 surfaces. Fully inherited
// placements coalesce, so depth alone must not push them back onto the CPU.
void testDeepGPUChain() {
    Fixture f; RecordingBackend renderer; renderer.gpu = true; krkrsdl3::backend = &renderer;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    emotenode* parent = nullptr;
    for (int depth = 0; depth < 40; ++depth) parent = f.node("layout" + std::to_string(depth), "layout", parent);
    auto* node = f.node("icon", "icon", parent); node->frameList.front()->hasbp = true; f.step(0);
    auto* ref = f.ref(node);
    check(ref->renderMethod.size() > 40, "icon sits under a deep layout chain");
    check(ref->_useGPUDeform && ref->_meshVertices.empty(), "deep inherited chain deforms on the GPU");
    check(emoteplayer::emoteSurfaceContributionCount(ref->renderMethod) <= 3, "inherited placements coalesce");

    // Every partially inherited ancestor keeps its own contribution; a chain
    // that overflows the shader's slots keeps the exact CPU mesh.
    std::vector<emoteRender> partial(40);
    for (auto& surface : partial) surface.currInheritMask = emoteplayer::emoteLinearInheritMask & ~0x10u;
    std::vector<krkrsdl3::TVPMeshDeformSurface> surfaces;
    check(emoteplayer::emoteSurfaceContributionCount(partial) == partial.size() + 1, "partial inheritance counted per surface");
    check(!emoteplayer::buildGPUDeformSurfaces(partial, surfaces), "overflowing chain stays on the CPU");
    partial.resize(emoteplayer::emoteMaxGPUDeformContributions - 1);
    check(emoteplayer::buildGPUDeformSurfaces(partial, surfaces), "chain that fits the shader slots is accepted");
    krkrsdl3::backend = nullptr;
}

// Hit-test transparency hints rely on mayCoverPixel never missing a pixel the
// rasterizer can write: it must contain every exact hit and stay bounded.
void testCoverage() {
    using emoteplayer::EmoteVertex;
    const std::vector<EmoteVertex> tri{{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}};
    const std::vector<uint16_t> idx{0, 1, 2};
    check(emoteplayer::nearMesh(tri, idx, .2f, .2f, 0), "point inside triangle covered");
    check(!emoteplayer::nearMesh(tri, idx, .6f, .6f, .1f), "point beyond margin not covered");
    check(emoteplayer::nearMesh(tri, idx, .6f, .6f, .2f), "point within margin of hypotenuse covered");
    check(emoteplayer::nearMesh(tri, idx, -.05f, .5f, .1f), "point within margin of an axis edge covered");
    check(!emoteplayer::nearMesh({{0, 0, 0, 0}, {1, 1, 0, 0}, {2, 2, 0, 0}}, idx, 1, 1, .5f), "degenerate triangle draws nothing");

    Fixture f; RecordingBackend renderer; krkrsdl3::backend = &renderer;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    auto* node = f.node("icon", "icon");
    const auto sweep = [&](const char* what) {
        emoteplayer::EmoteHitFrame frame{f.engine._mainMotionRef, glm::mat4(1.0f), 200, 200};
        int covered = 0;
        for (int y = 0; y < 200; y += 2)
            for (int x = 0; x < 200; x += 2) {
                const bool may = frame.mayCoverPixel(x, y);
                if (frame.contains(nullptr, x + .5f, y + .5f)) check(may, what);
                covered += may;
            }
        return covered;
    };
    f.step(0); f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    const int cpu = sweep("CPU mesh hit outside coverage");
    check(cpu > 0 && cpu < 100 * 100, "CPU icon covers part of the target");
    node->frameList.front()->hasbp = true; renderer.gpu = true; f.step(3); f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(f.ref(node)->_useGPUDeform && sweep("GPU-deformed hit outside coverage") > 0, "GPU-deformed icon covered");
    node->frameList.front()->opa = 0; f.step(9); f.engine.draw(&renderer, &renderer, f.limit, nullptr);
    check(sweep("undrawn") == 0, "undrawn frame covers nothing");
    check(emoteplayer::EmoteHitFrame{}.mayCoverPixel(0, 0), "unknown frame size is conservative");
    krkrsdl3::backend = nullptr;
}

void testDrawOrder() {
    Fixture f; auto* a = f.node("a"); auto* b = f.node("b");
    a->frameList.front()->coordZ = .1; b->frameList.front()->coordZ = .2;
    f.step(0); check(f.engine._mainMotionRef->drawNodes().front()->currentNode == b, "depth sorting uses production draw list");
    f.step(1); check(f.engine._mainMotionRef->drawNodes().front()->currentNode == b, "unchanged depth keeps ordering");
    b->frameList.front()->coordZ = -.1; f.step(2);
    check(f.engine._mainMotionRef->drawNodes().front()->currentNode == a, "changed depth rebuilds ordering");
}

void testEarlySeek() {
    Fixture f; auto* parent = f.node("parent", "layout"); auto* shape = f.node("shape", "shape/circle", parent);
    f.next(parent, 60, 15); f.next(shape, 60, -10);
    for (float tick : {0.f, 1.f, 10.f, 30.f, 59.f, 60.f, 70.f, 2.f, 2.f}) f.step(tick);
    // A warm node can seek before its first keyframe. It must not dereference
    // a null frame left by checkDrawStatus or retain an earlier visible shape.
    shape->frameList[0]->time = 10; f.step(20); f.step(0);
    check(f.ref(shape)->frame == nullptr && f.engine._mainMotionRef->shapeNodeAreas.empty(), "early seek invalidates warm node safely");
}

void testSourceResolution() {
    Fixture f; auto* blank = f.node("blank", "src/blank/10:20:3:4");
    f.step(0); auto* ref = f.ref(blank);
    check(ref->isNeedDraw && near(ref->width, 10) && near(ref->height, 20) &&
          near(ref->originX, 3) && near(ref->originY, 4), "blank source parsed");
    f.step(1);
    check(near(ref->width, 10) && near(ref->originY, 4), "resolved blank source reused");
    blank->frameList[0]->src = "blank/30:40:1:2"; f.step(2);
    check(near(ref->width, 30) && near(ref->height, 40) && near(ref->originX, 1), "edited src re-resolves");
    blank->frameList[0]->src = "nonsense"; f.step(3);
    check(!ref->isNeedDraw && !ref->isShape && !ref->isIcon, "unsupported source is not drawn");
    blank->frameList[0]->src = "shape/rect"; f.step(4);
    check(ref->isShape && !ref->isNeedDraw, "unsupported source recovers when src changes");
}

// Pass any argument to time production progress() for a synthetic scene.
double benchmark(int animatedEvery, int players) {
    std::vector<std::unique_ptr<Fixture>> instances;
    for (int p = 0; p < players; ++p) {
        instances.push_back(std::make_unique<Fixture>());
        for (int i = 0; i < 160; ++i) {
            auto* node = instances.back()->node("node" + std::to_string(i), i % 3 == 0 ? "shape/circle" : "shape/rect");
            if (animatedEvery && i % animatedEvery == 0) instances.back()->next(node, 60, 10);
        }
        instances.back()->step(0);
    }
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 1200; ++frame)
        for (auto& instance : instances) instance->step(float(frame % 60));
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
// Chains of nested Bezier layouts ending in a GPU-deformed icon, like E-mote
// characters, with every node animated. Returns microseconds per node update.
double deepBenchmark(int depth) {
    RecordingBackend renderer; renderer.gpu = true; krkrsdl3::backend = &renderer;
    Fixture f;
    emotesource source{&f.file, 0}; emoteicon icon{&f.file, 0};
    icon.width = icon.height = 32; icon.originX = icon.originY = 16; icon.selftexture = &icon;
    source.icon["icon"] = &icon; f.file._source["source"] = &source;
    int count = 0;
    for (int chain = 0; chain < 300 / depth; ++chain) {
        emotenode* parent = nullptr;
        for (int d = 0; d < depth; ++d, ++count) {
            parent = f.node("character_part_" + std::to_string(chain) + "_" + std::to_string(d),
                            d + 1 == depth ? "icon" : "layout", parent);
            parent->frameList.front()->hasbp = d + 1 < depth;
            f.next(parent, 60, 10);
        }
    }
    f.step(0);
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 600; ++frame) f.step(float(frame % 60));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    krkrsdl3::backend = nullptr;
    return ms * 1000.0 / 600 / count;
}
void runBenchmarks() {
    for (int players : {1, 2}) for (int animatedEvery : {0, 4, 1})
        std::cout << "BENCH players=" << players << " animated=" << (animatedEvery ? 100 / animatedEvery : 0)
            << "% progress_ms=" << benchmark(animatedEvery, players) << '\n';
    for (int depth : {1, 8, 16})
        std::cout << "BENCH deep depth=" << depth << " us_per_node=" << deepBenchmark(depth) << '\n';
}
}
int main(int argc, char**) {
    try {
        testLazyShapeGeometry(); testMaskPrepass(); testSharedChain(); testPhysicsSample();
        testStaticAndDependencies(); testParentAndSubmotion(); testVariablesAndIsolation();
        testSharedSentinelsAndLoop(); testIconAndGPU(); testDeepGPUChain(); testDrawOrder(); testEarlySeek(); testSourceResolution(); testCoverage();
        std::cout << "Production node/geometry: " << checks << " checks passed\n";
        if (argc > 1) runBenchmarks();
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
