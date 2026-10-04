#include "PointReadTrace.h"
#include "AsyncAlphaTileCache.h"
#include "../../Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/emoteperformance.h"
#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

namespace trace = krkrsdl3::point_trace;
using tjs_int = int;
using tjs_uint32 = std::uint32_t;
using tjs_int64 = std::int64_t;
using tjs_real = double;
using ttstr = std::string;
enum tTVPMouseButton { mbLeft };
constexpr int ltOpaque = 1;
enum tTVPHitType { htMask, htProvince, htOpaque };
struct TestRect { int left=0,top=0,right=128,bottom=128;
    int get_width() const { return right-left; } int get_height() const { return bottom-top; } };
static uint64_t nextTextureID=1, currentFrameSerial=1;
static std::shared_ptr<krkrsdl3::AsyncLayerPresentation> currentAlphaPresentation;
static auto TVPGetEmoteAlphaPresentation() { return currentAlphaPresentation; }
struct iTVPTexture2D {
    uint64_t identity=nextTextureID++, version=1;
    int width=128,height=128, syncReads=0;
    uint8_t alpha=255;
    bool gpu=true;
    int refs=1;
    void AddRef() { ++refs; } void Release() { --refs; }
    krkrsdl3::AsyncAlphaTileCache tiles;
    bool GetContentKey(uint64_t& id,uint64_t& v) const { id=identity; v=version; return true; }
    uint32_t GetPointAlpha(int,int) { ++syncReads; return alpha; }
};
struct TestImage {
    iTVPTexture2D texture;
    iTVPTexture2D* GetTexture() { return &texture; }
    int GetBPP() const { return 32; }
    int GetWidth() const { return texture.width; } int GetHeight() const { return texture.height; }
    uint32_t GetPoint(int x,int y) { return texture.GetPointAlpha(x,y)<<24; }
};
static bool TVPIsEmoteAsyncAlphaTexture(iTVPTexture2D* t) {
    return t && t->gpu && emoteplayer::performanceEnabled("MIKAGE_EMOTE_ASYNC_ALPHA");
}
static bool TVPRequestEmoteAsyncAlpha(iTVPTexture2D* t,int x,int y,
                                    std::shared_ptr<krkrsdl3::AsyncAlphaTile>& tile) {
    if(!TVPIsEmoteAsyncAlphaTexture(t)) return false;
    tile=t->tiles.Demand(x,y,t->width,t->height); return true;
}
static uint64_t TVPGetEmoteAlphaPresentationSerial() { return currentFrameSerial; }
static void TVPEncodeFrozenEmoteAsyncAlpha(iTVPTexture2D*,
    const std::shared_ptr<krkrsdl3::AsyncLayerPresentation>&);

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> static void requireThrows(F action) {
    bool thrown = false;
    try { action(); } catch (const std::runtime_error&) { thrown = true; }
    require(thrown, "expected callback exception was swallowed");
}
struct iTJSDispatch2 {
    int refs = 1;
    void AddRef() { ++refs; }
    void Release() { --refs; }
};
class tTVPLayerManager;
struct tTJSNI_BaseLayer {
    iTJSDispatch2 object;
    iTJSDispatch2* Owner = &object;
    tTJSNI_BaseLayer* hit = this;
    tTVPLayerManager* manager = nullptr;
    int queries = 0, clicks = 0, doubleClicks = 0, downs = 0, ups = 0, touches = 0;
    int enters = 0, leaves = 0, moves = 0, queryX = 0, queryY = 0;
    int eventX = 0, eventY = 0;
    double touchX=0,touchY=0;
    std::vector<std::pair<char,tjs_uint32>> touchEvents;
    trace::Trigger queryTrigger = trace::Trigger::Unknown;
    trace::Trigger queryParent = trace::Trigger::Unknown;
    std::function<void()> onQuery, onDown, onEnter;
    TestImage* MainImage=nullptr;
    TestImage* ProvinceImage=nullptr;
    TestRect Rect;
    int ImageLeft=0,ImageTop=0,HitThreshold=16,updates=0;
    tTVPHitType HitType=htMask;
    bool visible=true;
    int offsetX=10,offsetY=20;
    TestImage* GetMainImage() { return MainImage; }
    bool GetNodeVisible() const { return visible; }
    const TestRect& GetRect() const { return Rect; }
    tTVPHitType GetHitType() const { return HitType; }
    int GetHitThreshold() const { return HitThreshold; }
    int GetImageLeft() const { return ImageLeft; } int GetImageTop() const { return ImageTop; }
    void Update() { ++updates; }
    bool _HitTestNoVisibleCheck(int,int);
    iTJSDispatch2* GetOwnerNoAddRef() { return Owner; }
    void GetMostFrontChildAt(int,int,tTJSNI_BaseLayer**,tTJSNI_BaseLayer*,bool);
    void FromPrimaryCoordinates(int& x, int& y) { x -= offsetX; y -= offsetY; }
    void FromPrimaryCoordinates(double& x, double& y) { x -= offsetX; y -= offsetY; }
    void FireClick(int x, int y) { ++clicks; eventX = x; eventY = y; }
    void FireDoubleClick(int, int) { ++doubleClicks; }
    void FireMouseDown(int x, int y, tTVPMouseButton, tjs_uint32) {
        ++downs; eventX = x; eventY = y; if (onDown) onDown();
    }
    void FireMouseUp(int, int, tTVPMouseButton, tjs_uint32) { ++ups; }
    void FireMouseEnter() { ++enters; if (onEnter) onEnter(); }
    void FireMouseLeave() { ++leaves; }
    void FireMouseMove(int, int, tjs_uint32) { ++moves; }
    void FireTouchDown(double x,double y,double,double,tjs_uint32 id) { ++touches; touchX=x;touchY=y;touchEvents.emplace_back('d',id); }
    void FireTouchUp(double x,double y,double,double,tjs_uint32 id) { ++ups; touchX=x;touchY=y;touchEvents.emplace_back('u',id); }
    void FireTouchMove(double x,double y,double,double,tjs_uint32 id) { ++moves; touchX=x;touchY=y;touchEvents.emplace_back('m',id); }
    void SetCurrentCursorToWindow();
    void SetCurrentHintToWindow();
};
struct iTVPLayerManager {
    virtual ~iTVPLayerManager() = default;
    int refs = 1, desiredType = 0;
    virtual void AddRef() { ++refs; }
    virtual void Release() { --refs; }
    virtual void SetDesiredLayerType(int value) { desiredType = value; }
    virtual tTJSNI_BaseLayer* GetPrimaryLayer() { return nullptr; }
};
struct iTVPLayerTreeOwner {
    virtual ~iTVPLayerTreeOwner() = default;
    int cursors = 0, hints = 0, released = 0, cursor = -1, registrations = 0;
    ttstr hint;
    iTJSDispatch2* sender = nullptr;
    std::function<void()> onCursor, onHint, onRegister, onUnregister;
    virtual void SetMouseCursor(tTVPLayerManager*, int value) {
        ++cursors; cursor = value; if (onCursor) onCursor();
    }
    virtual void SetHint(tTVPLayerManager*, iTJSDispatch2* from, const ttstr& value) {
        ++hints; hint = value; sender = from; if (onHint) onHint();
    }
    void ReleaseMouseCapture(tTVPLayerManager*) { ++released; }
    void RegisterLayerManager(tTVPLayerManager*) { ++registrations; if (onRegister) onRegister(); }
    void UnregisterLayerManager(tTVPLayerManager*) { --registrations; if (onUnregister) onUnregister(); }
};
class tTVPLayerManager : public iTVPLayerManager {
public:
    explicit tTVPLayerManager(iTVPLayerTreeOwner* owner) : LayerTreeOwner(owner) {}
    iTVPLayerTreeOwner* LayerTreeOwner;
    tTJSNI_BaseLayer *Primary = nullptr, *CaptureOwner = nullptr, *LastMouseMoveSent = nullptr;
    int LastMouseMoveX = -1, LastMouseMoveY = -1;
    bool InNotifyingHintOrCursorChange = false, ReleaseCaptureCalled = false;
    tjs_int64 ReleaseTouchCaptureIDMark = -1;
    tTJSNI_BaseLayer* touchCapture = nullptr;
    std::unordered_map<tjs_uint32,tTJSNI_BaseLayer*> touchCaptures;
    std::vector<tTJSNI_BaseLayer*> nodes;
    std::vector<tTJSNI_BaseLayer*>& GetAllNodes() {
        if(nodes.empty() && Primary) nodes.push_back(Primary); return nodes;
    }
#include "ProductionManagerMembers.inc"
    tTJSNI_BaseLayer* GetPrimaryLayer() override { return Primary; }
    void RegisterSelfToWindow();
    void UnregisterSelfFromWindow();
    void NotifyMouseCursorChange(tTJSNI_BaseLayer*, tjs_int);
    void SetMouseCursor(tjs_int);
    void NotifyHintChange(tTJSNI_BaseLayer*, const ttstr&);
    void SetHint(iTJSDispatch2*, const ttstr&);
    void SetLayerTreeOwner(iTVPLayerTreeOwner*);
    tTJSNI_BaseLayer* GetMostFrontChildAt(int, int, tTJSNI_BaseLayer* = nullptr, bool = false);
    void PrimaryClick(int, int);
    void PrimaryDoubleClick(int, int);
    void PrimaryMouseDown(int, int, tTVPMouseButton, tjs_uint32);
    void PrimaryMouseUp(int, int, tTVPMouseButton, tjs_uint32);
    void PrimaryMouseMove(int, int, tjs_uint32);
    void PrimaryTouchDown(double, double, double, double, tjs_uint32);
    void PrimaryTouchUp(double, double, double, double, tjs_uint32);
    void PrimaryTouchMove(double, double, double, double, tjs_uint32);
    tTJSNI_BaseLayer* GetTouchCapture(tjs_uint32 id) {
        auto entry=touchCaptures.find(id); return entry==touchCaptures.end() ? nullptr : entry->second;
    }
    void ReleaseCapture();
    void ReleaseTouchCapture(tjs_uint32 id) { touchCaptures.erase(id); if(touchCaptures.empty()) touchCapture=nullptr; }
    void SetTouchCapture(tjs_uint32 id,tTJSNI_BaseLayer* layer) { touchCapture=layer; touchCaptures[id]=layer; }
};
void tTJSNI_BaseLayer::SetCurrentCursorToWindow() { if (manager) manager->SetMouseCursor(7); }
void tTJSNI_BaseLayer::SetCurrentHintToWindow() { if (manager) manager->SetHint(Owner, "hover"); }
static bool TVPIsAnyMouseButtonPressedInShiftStateFlags(tjs_uint32 flags) { return flags != 0; }

struct TVPWindow : iTVPLayerTreeOwner {
#include "ProductionWindowMembers.inc"
    void ReleaseBorrowedTexture() {}
};
struct ForeignWindow : TVPWindow {
    int customCursors = 0, customHints = 0;
    void SetMouseCursor(tTVPLayerManager* manager, int value) override {
        ++customCursors; TVPWindow::SetMouseCursor(manager, value);
    }
    void SetHint(tTVPLayerManager* manager, iTJSDispatch2* from, const ttstr& value) override {
        ++customHints; TVPWindow::SetHint(manager, from, value);
    }
};
static void TVPThrowExceptionMessage(const char* message) { throw std::runtime_error(message); }
static constexpr auto TVPBasicDrawDeviceDoesNotSupporteLayerManagerMoreThanOne = "one manager only";
#define TVPThrowInternalError throw std::runtime_error("internal error")
class tTVPBasicDrawDevice {
public:
    TVPWindow* Window = nullptr;
    iTVPLayerManager* Manager = nullptr;
    virtual ~tTVPBasicDrawDevice();
    void SetWindowInterface(TVPWindow*);
    void UpdatePointerPresentationHitTesting();
    void AddLayerManager(iTVPLayerManager*);
    void RemoveLayerManager(iTVPLayerManager*);
};
struct DummyBackend {
    void DestroyTexture(void*) {}
    void DestroyTarget(void*) {}
};
class DrawDeviceD3D {
public:
    struct ManagerInfo { iTVPLayerManager* Manager; tTJSNI_BaseLayer* Primary; };
    std::vector<ManagerInfo> Managers;
    TVPWindow* Window = nullptr;
    DummyBackend* Backend = nullptr;
    void *PresentScratchTexture = nullptr, *CompositeTarget = nullptr;
    void *PrevCompositeTarget = nullptr, *ScratchTexture = nullptr;
    virtual ~DrawDeviceD3D();
    void SetWindowInterface(TVPWindow*);
    void UpdatePointerPresentationHitTesting();
    void AddLayerManager(iTVPLayerManager*);
    void RemoveLayerManager(iTVPLayerManager*);
};
class D3D : public DrawDeviceD3D {};
class ForeignBasic : public tTVPBasicDrawDevice {};
class ForeignD3D : public DrawDeviceD3D {};
class ForeignScriptD3D : public D3D {};

#include "ProductionInputMethods.inc"
#define Manager manager
#include "ProductionInputHitTest.inc"
#undef Manager
void tTJSNI_BaseLayer::GetMostFrontChildAt(int x,int y,tTJSNI_BaseLayer** out,tTJSNI_BaseLayer*,bool) {
    ++queries; queryX=x; queryY=y;
    queryTrigger=trace::origin.trigger; queryParent=trace::origin.parentTrigger;
    if(onQuery) onQuery();
    *out=hit;
    if(hit && hit->MainImage) {
        int localX=x,localY=y; bool inside=true;
        if(manager && manager->IsAsyncAlphaQuery()) {
            if(!manager->GetPinnedLayerPoint(hit,localX,localY,inside) || !inside) { *out=nullptr; return; }
        } else hit->FromPrimaryCoordinates(localX,localY);
        if(!hit->_HitTestNoVisibleCheck(localX,localY)) *out=nullptr;
    }
}

struct Fixture {
    iTVPLayerTreeOwner owner;
    tTVPLayerManager manager{&owner};
    tTJSNI_BaseLayer primary, other;
    Fixture() {
        manager.Primary = &primary; primary.manager = &manager;
        manager.LastMouseMoveX = 45; manager.LastMouseMoveY = 68;
    }
};

static void propertyCallbacks() {
    Fixture f;
    require(f.manager.PointerPresentationHitTestingEnabled, "new/custom managers must default to enabled");
    f.manager.NotifyMouseCursorChange(&f.primary, 12);
    require(f.primary.queries == 1 && f.primary.queryX == 45 && f.primary.queryY == 68,
            "cursor must query current pointer position");
    require(f.owner.cursors == 1 && f.owner.cursor == 12, "custom owner lost cursor callback");
    require(f.primary.queryTrigger == trace::Trigger::CursorChange, "cursor trace attribution missing");
    f.manager.NotifyHintChange(&f.primary, "text");
    require(f.owner.hints == 1 && f.owner.hint == "text" && f.owner.sender == f.primary.Owner,
            "custom owner lost hint or sender");
    require(f.primary.queryTrigger == trace::Trigger::HintChange, "hint trace attribution missing");
    f.manager.NotifyMouseCursorChange(&f.other, 99);
    f.manager.NotifyHintChange(&f.other, "not under pointer");
    require(f.owner.cursors == 1 && f.owner.hints == 1, "non-hit layer changed presentation");
    int queries = f.primary.queries;
    f.manager.CaptureOwner = &f.other;
    f.manager.NotifyMouseCursorChange(&f.other, 21);
    f.manager.NotifyHintChange(&f.other, "captured");
    require(f.primary.queries == queries && f.owner.cursor == 21 && f.owner.sender == f.other.Owner,
            "capture owner must select presentation without hit testing");
    f.manager.SetPointerPresentationHitTestingEnabled(false);
    f.manager.NotifyMouseCursorChange(&f.other, 90);
    f.manager.NotifyHintChange(&f.other, "disabled");
    f.manager.CaptureOwner = nullptr;
    f.manager.NotifyMouseCursorChange(&f.primary, 90);
    f.manager.NotifyHintChange(&f.primary, "disabled");
    require(f.primary.queries == queries && f.owner.cursor == 21 && f.owner.hint == "captured",
            "disabled presentation still performed property work");
    require(!f.manager.InNotifyingHintOrCursorChange, "early return left reentry guard set");
}

static void reentryAndExceptions() {
    Fixture f;
    f.primary.onQuery = [&] {
        f.manager.NotifyMouseCursorChange(&f.primary, 999);
        f.manager.NotifyHintChange(&f.primary, "nested hit callback");
    };
    f.owner.onCursor = [&] {
        f.manager.NotifyMouseCursorChange(&f.primary, 999);
        f.manager.NotifyHintChange(&f.primary, "nested");
    };
    f.manager.NotifyMouseCursorChange(&f.primary, 5);
    require(f.primary.queries == 1 && f.owner.cursors == 1 && f.owner.hints == 0,
            "hit-test or cursor callback reentry was not suppressed");
    f.primary.onQuery = {};
    f.owner.onCursor = {};
    f.owner.onHint = [&] { f.manager.NotifyMouseCursorChange(&f.primary, 999); };
    f.manager.NotifyHintChange(&f.primary, "outer");
    require(f.primary.queries == 2 && f.owner.cursors == 1, "hint-to-cursor reentry not suppressed");
    f.owner.onHint = {};
    for (bool cursor : {true, false}) {
        auto notify = [&] {
            if (cursor) f.manager.NotifyMouseCursorChange(&f.primary, 8);
            else f.manager.NotifyHintChange(&f.primary, "after throw");
        };
        f.primary.onQuery = [] { throw std::runtime_error("query failure"); };
        requireThrows(notify);
        require(!f.manager.InNotifyingHintOrCursorChange, "query exception left guard set");
        f.primary.onQuery = {};
        auto& callback = cursor ? f.owner.onCursor : f.owner.onHint;
        callback = [] { throw std::runtime_error("owner failure"); };
        requireThrows(notify);
        require(!f.manager.InNotifyingHintOrCursorChange, "owner exception left guard set");
        callback = {};
        int before = f.primary.queries;
        notify();
        require(f.primary.queries == before + 1, "notifications did not recover after exception");
    }
    {
        trace::TriggerScope down(trace::Trigger::PointerDown);
        f.manager.NotifyMouseCursorChange(&f.primary, 10);
        require(f.primary.queryTrigger == trace::Trigger::CursorChange &&
                f.primary.queryParent == trace::Trigger::PointerDown,
                "property lookup lost parent input trigger");
        require(trace::origin.trigger == trace::Trigger::PointerDown,
                "property trace leaked into parent input");
    }
    require(trace::origin.trigger == trace::Trigger::Unknown, "property trace leaked outside scope");
}

static void realInputStillQueries() {
    Fixture f;
    f.manager.SetPointerPresentationHitTestingEnabled(false);
    f.manager.PrimaryMouseDown(75, 92, mbLeft, 1);
    require(f.primary.queries > 0 && f.primary.queryTrigger == trace::Trigger::PointerDown,
            "presentation opt-out disabled or relabeled real pointer-down lookup");
    require(f.primary.downs == 1 && f.primary.eventX == 65 && f.primary.eventY == 72 &&
            f.manager.CaptureOwner == &f.primary, "mouse down dispatch/capture changed");
    int before = f.primary.queries;
    f.manager.PrimaryClick(76, 93);
    require(f.primary.queries == before + 1 && f.primary.clicks == 1 &&
            f.primary.queryTrigger == trace::Trigger::Click, "click did not revalidate captured target");
    f.primary.hit = &f.other;
    f.manager.PrimaryClick(76, 93);
    require(f.primary.clicks == 1 && f.other.clicks == 0, "click leaked to a different hit target");
    f.primary.hit = &f.primary;
    f.manager.PrimaryMouseUp(76, 93, mbLeft, 0);
    require(f.primary.ups == 1 && !f.manager.CaptureOwner && f.owner.released == 1,
            "mouse up failed to release capture");
    before = f.primary.queries;
    f.manager.PrimaryMouseMove(88, 99, 0);
    require(f.primary.queries > before && f.primary.moves > 0, "pointer move lost hit detection");
    before = f.primary.queries;
    f.manager.PrimaryDoubleClick(88, 99);
    require(f.primary.queries == before + 1 && f.primary.doubleClicks == 1, "double click lost lookup");
    before = f.primary.queries;
    f.manager.PrimaryTouchDown(90, 100, 1, 1, 3);
    require(f.primary.queries == before + 1 && f.primary.touches == 1 &&
            f.manager.touchCapture == &f.primary, "touch down lost lookup or capture");
    f.primary.onDown = [&] { f.manager.ReleaseCapture(); };
    f.manager.PrimaryMouseDown(91, 101, mbLeft, 1);
    require(!f.manager.CaptureOwner, "explicit release in mouse-down callback was ignored");
}

template<class Device> static void builtInLifecycle() {
    TVPWindow window;
    tTVPLayerManager manager(&window);
    {
        Device device;
        device.AddLayerManager(&manager);
        require(manager.PointerPresentationHitTestingEnabled, "unattached device disabled presentation");
        device.SetWindowInterface(&window);
        require(!manager.PointerPresentationHitTestingEnabled && manager.refs == 2,
                "built-in no-op window did not disable property hit testing");
        device.SetWindowInterface(nullptr);
        require(manager.PointerPresentationHitTestingEnabled, "window detach did not restore presentation");
        device.SetWindowInterface(&window);
        device.RemoveLayerManager(&manager);
        require(manager.PointerPresentationHitTestingEnabled && manager.refs == 1,
                "manager removal did not restore presentation/references");
        device.AddLayerManager(&manager);
        require(!manager.PointerPresentationHitTestingEnabled, "window-first attachment failed");
    }
    require(manager.PointerPresentationHitTestingEnabled && manager.refs == 1,
            "device destructor did not restore manager behavior/references");
    iTVPLayerTreeOwner foreignOwner;
    manager.SetLayerTreeOwner(&foreignOwner);
    {
        Device device;
        device.SetWindowInterface(&window);
        device.AddLayerManager(&manager);
        require(manager.PointerPresentationHitTestingEnabled, "foreign owner was opted out");
    }
    iTVPLayerManager foreignManager;
    {
        Device device;
        device.SetWindowInterface(&window);
        device.AddLayerManager(&foreignManager);
        require(foreignManager.refs == 2, "foreign manager reference missing");
        device.RemoveLayerManager(&foreignManager);
    }
    require(foreignManager.refs == 1, "foreign manager reference unbalanced");
}
template<class Device> static void foreignSubclass() {
    TVPWindow window;
    tTVPLayerManager manager(&window);
    Device device;
    device.SetWindowInterface(&window);
    device.AddLayerManager(&manager);
    require(manager.PointerPresentationHitTestingEnabled, "native subclass callback was opted out");
}
template<class Device> static void foreignWindow() {
    ForeignWindow window;
    tTVPLayerManager manager(&window);
    tTJSNI_BaseLayer layer;
    manager.Primary = &layer;
    Device device;
    device.SetWindowInterface(&window);
    device.AddLayerManager(&manager);
    require(!window.HasNativePointerPresentation(), "test must inherit the native no-op capability");
    require(manager.PointerPresentationHitTestingEnabled, "window subclass callbacks were opted out");
    manager.NotifyMouseCursorChange(&layer, 17);
    manager.NotifyHintChange(&layer, "custom");
    require(layer.queries == 2 && window.customCursors == 1 && window.customHints == 1,
            "custom window lost virtual owner callbacks or property hit tests");
}

static void ownerChangesReset() {
    Fixture f;
    iTVPLayerTreeOwner replacement;
    f.manager.SetPointerPresentationHitTestingEnabled(false);
    f.manager.SetLayerTreeOwner(&replacement);
    require(f.manager.PointerPresentationHitTestingEnabled, "owner replacement retained old capability");
    f.manager.NotifyHintChange(&f.primary, "new owner");
    require(replacement.hints == 1, "replacement owner lost presentation callbacks");
    f.manager.SetPointerPresentationHitTestingEnabled(false);
    replacement.onRegister = [&] {
        require(f.manager.PointerPresentationHitTestingEnabled, "registration observed stale capability");
        f.manager.SetPointerPresentationHitTestingEnabled(false);
    };
    f.manager.RegisterSelfToWindow();
    require(!f.manager.PointerPresentationHitTestingEnabled,
            "registering owner could not negotiate its presentation capability");
    replacement.onUnregister = [&] {
        require(f.manager.PointerPresentationHitTestingEnabled, "unregistration observed stale capability");
    };
    f.manager.UnregisterSelfFromWindow();
    require(f.manager.PointerPresentationHitTestingEnabled && replacement.registrations == 0,
            "unregistration failed to restore default");
}

static void multipleManagers() {
    TVPWindow window;
    iTVPLayerTreeOwner foreignOwner;
    tTVPLayerManager first(&window), second(&window), foreign(&foreignOwner);
    {
        DrawDeviceD3D device;
        device.SetWindowInterface(&window);
        device.AddLayerManager(&first);
        device.AddLayerManager(&second);
        device.AddLayerManager(&foreign);
        require(!first.PointerPresentationHitTestingEnabled && !second.PointerPresentationHitTestingEnabled &&
                foreign.PointerPresentationHitTestingEnabled, "multi-manager capability crossed owners");
        device.RemoveLayerManager(&first);
        require(first.PointerPresentationHitTestingEnabled && !second.PointerPresentationHitTestingEnabled,
                "removing one manager changed retained manager behavior");
        device.SetWindowInterface(nullptr);
        require(second.PointerPresentationHitTestingEnabled && foreign.PointerPresentationHitTestingEnabled,
                "window detach failed to reset all managers");
        device.SetWindowInterface(&window);
    }
    require(first.refs == 1 && second.refs == 1 && foreign.refs == 1 &&
            second.PointerPresentationHitTestingEnabled && foreign.PointerPresentationHitTestingEnabled,
            "multi-manager destruction leaked state or references");
}

struct TestAlphaBudget { size_t active=0, peak=0, limit=size_t(-1); };
static auto alphaBudget=std::make_shared<TestAlphaBudget>();
static bool completeEncodedAlpha=true;
static std::vector<std::weak_ptr<krkrsdl3::AsyncLayerReadback>> encodedAlphaReads;
static std::shared_ptr<krkrsdl3::AsyncLayerPresentation> produceAlpha(
    iTVPTexture2D& texture,uint64_t serial,bool completed=true,bool presented=true,
    std::shared_ptr<krkrsdl3::AsyncLayerPresentation> ticket={}) {
    if(!ticket) ticket=std::make_shared<krkrsdl3::AsyncLayerPresentation>(serial);
    ticket->presented.store(presented);
    texture.tiles.EncodeDemanded(texture.version,ticket,[&](const auto& read) {
        if(alphaBudget->active>=alphaBudget->limit) return false;
        struct Lease { std::shared_ptr<TestAlphaBudget> budget;
            explicit Lease(std::shared_ptr<TestAlphaBudget> b):budget(std::move(b)) {
                ++budget->active; budget->peak=std::max(budget->peak,budget->active);
            }
            ~Lease(){--budget->active;}
        };
        read->allocationLease=std::make_shared<Lease>(alphaBudget);
        read->pitch=read->region.Width()*4;
        read->rgba.resize(size_t(read->pitch)*read->region.Height(),0);
        for(size_t i=3;i<read->rgba.size();i+=4) read->rgba[i]=texture.alpha;
        read->completed.store(completed && completeEncodedAlpha);
        encodedAlphaReads.push_back(read); return true;
    });
    return ticket;
}
static void TVPEncodeFrozenEmoteAsyncAlpha(iTVPTexture2D* texture,
    const std::shared_ptr<krkrsdl3::AsyncLayerPresentation>& ticket) {
    produceAlpha(*texture,ticket->frameSerial,true,ticket->presented.load(),ticket);
}
static void completeAlphaRequests() {
    for(auto it=encodedAlphaReads.begin();it!=encodedAlphaReads.end();) {
        if(auto read=it->lock()) { read->completed.store(true); ++it; }
        else it=encodedAlphaReads.erase(it);
    }
}
static auto composeAlpha(tTVPLayerManager& manager,uint64_t serial,bool presented=true) {
    auto ticket=std::make_shared<krkrsdl3::AsyncLayerPresentation>(serial);
    ticket->presented.store(presented); currentAlphaPresentation=ticket;
    manager.BindAlphaPresentation();
    manager.FinishAlphaPresentation(); // Older frozen requests have FIFO priority.
    for(auto* layer:manager.GetAllNodes()) {
        manager.CaptureAlphaForPresentation(layer);
        if(layer->MainImage) produceAlpha(layer->MainImage->texture,serial,true,presented,ticket);
    }
    manager.FinishAlphaPresentation();
    return ticket;
}
static void asyncInputChain() {
    SDL_SetHint("MIKAGE_EMOTE_ASYNC_ALPHA","1");
    {
        Fixture f; TestImage image; f.primary.MainImage=&image;
        std::vector<int> order;
        f.primary.onDown=[&]{ order.push_back(1); };
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        f.manager.PrimaryClick(45,68);
        f.manager.PrimaryMouseUp(45,68,mbLeft,0);
        require(f.primary.downs==0 && f.primary.clicks==0 && f.primary.ups==0,
                "cold alpha cache dispatched a partial pointer chain");
        require(image.texture.syncReads==0,"cold pointer synchronously read GPU alpha");
        auto ticket=composeAlpha(f.manager,1,false);
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==0,"GPU completion published an unpresented input frame");
        f.primary.offsetX=100; f.primary.offsetY=200;
        ++image.texture.version;
        ticket->presented.store(true);
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==1 && f.primary.clicks==1 && f.primary.ups==1 && order==std::vector<int>{1},
                "pending down/click/up order or exact once dispatch changed");
        require(f.primary.eventX==35 && f.primary.eventY==48,
                "deferred event used a new coordinate mapping");
        require(image.texture.syncReads==0,"ready alpha event chased a newer texture via sync read");
        // Explicit queries remain immediate even while the optimization is on.
        require(f.primary._HitTestNoVisibleCheck(35,48) && image.texture.syncReads==1,
                "explicit script hit query borrowed the UI frame");
    }
    {
        Fixture f; TestImage first,second; f.primary.MainImage=&first; f.other.MainImage=&second;
        f.other.manager=&f.manager; f.manager.nodes={&f.primary,&f.other};
        currentFrameSerial=20;
        f.manager.PrimaryTouchDown(45.25,68.5,0,0,1);
        second.texture.alpha=0; produceAlpha(second.texture,19);
        second.texture.alpha=255; ++second.texture.version;
        auto a=std::make_shared<krkrsdl3::AsyncLayerPresentation>(20); a->presented.store(true);
        currentAlphaPresentation=a; f.manager.BindAlphaPresentation();
        f.manager.CaptureAlphaForPresentation(&f.primary); f.manager.CaptureAlphaForPresentation(&f.other);
        produceAlpha(first.texture,20,true,true,a);
        alphaBudget->limit=0;
        f.manager.FinishAlphaPresentation();
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.touches==0,"mask candidates from different display frames were combined");
        require(f.manager.PendingAlphaInput.front()->presentation==a,"partial read budget discarded the chosen display frame");
        alphaBudget->limit=size_t(-1);
        composeAlpha(f.manager,21);
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.touches==1 && first.texture.syncReads==0 && second.texture.syncReads==0,
                "common displayed frame did not resolve touch without a GPU wait");
    }
    {
        Fixture f; TestImage image; f.primary.MainImage=&image;
        currentFrameSerial=30;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        f.manager.PrimaryMouseMove(46,68,1);
        f.manager.PrimaryMouseUp(46,68,mbLeft,0);
        f.manager.nodes={&f.other};
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==0 && f.primary.moves==0 && f.primary.ups==0,
                "destroyed alpha candidate canceled only part of a pointer chain");
        f.manager.PrimaryMouseUp(46,68,mbLeft,0);
        require(f.other.ups==0,"late up from canceled chain was retargeted to another layer");
    }
    {
        Fixture f; TestImage image; f.primary.MainImage=&image; image.texture.alpha=15;
        currentFrameSerial=40;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        composeAlpha(f.manager,40);
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==0 && image.texture.syncReads==0,"alpha below hitThreshold received input");
        image.texture.alpha=16; ++image.texture.version; currentFrameSerial=45;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        composeAlpha(f.manager,45);
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==1 && image.texture.syncReads==0,"alpha equal to hitThreshold rejected input");
    }
    {
        Fixture f; TestImage oldImage,newImage; f.primary.MainImage=&oldImage;
        currentFrameSerial=50;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        f.primary.MainImage=&newImage; newImage.texture.width=48;
        f.primary.offsetX=20;
        auto ticket=composeAlpha(f.manager,50,false);
        f.primary.offsetX=100; // The submitted frame retains offset 20.
        ticket->presented.store(true); f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==1 && f.primary.eventX==25 && newImage.texture.syncReads==0,
                "resize/texture epoch did not bind the first composed frame's mapping");
    }
    {
        Fixture f; TestImage image; f.primary.MainImage=&image;
        asyncInputManagers.push_back(&f.manager);
        for(int i=0;i<128;++i) f.manager.PrimaryMouseMove(45,68,0);
        require(TVPHasPendingLayerPointerBackpressure(),"pointer queue did not stop upstream input at its budget");
        composeAlpha(f.manager,60);
        f.manager.ProcessPendingAlphaInput();
        require(!TVPHasPendingLayerPointerBackpressure(),"ready GPU frames did not release pointer backpressure");
        while(!f.manager.PendingAlphaInput.empty()) f.manager.ProcessPendingAlphaInput();
        asyncInputManagers.erase(std::remove(asyncInputManagers.begin(),asyncInputManagers.end(),&f.manager),asyncInputManagers.end());
        require(image.texture.syncReads==0,"backpressure recovery synchronously read GPU alpha");
    }
    {
        Fixture f; TestImage image; f.primary.MainImage=&image;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        auto ticket=composeAlpha(f.manager,70,false);
        ticket->failed.store(true); f.manager.nodes={&f.other};
        const int updates=f.primary.updates;
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==0 && f.primary.updates==updates,
                "failed presentation retried a removed raw Layer pointer before validating its lifetime");
    }
    {
        Fixture f; TestImage image; f.primary.MainImage=&image;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        composeAlpha(f.manager,80);
        f.primary.Owner=&f.other.object; // Same native address, different owner epoch.
        f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==0,"an old pointer event targeted a recycled Layer owner");
        f.primary.Owner=&f.primary.object;
    }
    {
        Fixture f; TestImage image; f.primary.MainImage=&image;
        f.manager.PrimaryTouchDown(45.25,68.5,0,0,1);
        f.manager.PrimaryTouchDown(75.25,68.5,0,0,2);
        f.manager.PrimaryTouchMove(46.75,68.5,0,0,1);
        f.manager.PrimaryTouchUp(46.75,68.5,0,0,1);
        f.manager.PrimaryTouchUp(75.25,68.5,0,0,2);
        auto ticket=composeAlpha(f.manager,90,false);
        f.primary.offsetX=100; ticket->presented.store(true);
        f.manager.ProcessPendingAlphaInput();
        const std::vector<std::pair<char,tjs_uint32>> expected={{'d',1},{'d',2},{'m',1},{'u',1},{'u',2}};
        require(f.primary.touchEvents==expected && f.primary.touchX==65.25 && f.primary.touchY==48.5,
                "multi-pointer async replay changed identity/order/fractional coordinates");
        require(f.manager.AlphaPointerChains.empty() && f.manager.touchCaptures.empty(),
                "completed touch chains accumulated pointer-ID metadata or captures");
        require(image.texture.syncReads==0,"multi-pointer replay synchronously read GPU alpha");
    }
    {
        // Three changing candidates with only TWO concurrent read allocations:
        // one event must finish by supplementing immutable F, rather than
        // restarting all three candidates every frame and starving forever.
        Fixture f; tTJSNI_BaseLayer third;
        TestImage source[3],current[3],next[3];
        f.primary.MainImage=&source[0]; f.other.MainImage=&source[1]; third.MainImage=&source[2];
        f.other.manager=third.manager=&f.manager; f.manager.nodes={&f.primary,&f.other,&third};
        for(auto& image:source) image.texture.alpha=255;
        for(auto& image:current) image.texture.alpha=0;
        alphaBudget=std::make_shared<TestAlphaBudget>(); alphaBudget->limit=2; completeEncodedAlpha=false;
        f.manager.PrimaryMouseDown(45,68,mbLeft,1);
        auto chosen=composeAlpha(f.manager,100);
        require(f.primary.downs==0 && alphaBudget->active==2,"small budget did not keep incomplete event pending");
        f.primary.MainImage=&current[0]; f.other.MainImage=&current[1]; third.MainImage=&current[2];
        f.primary.offsetX=30;
        completeAlphaRequests(); f.manager.ProcessPendingAlphaInput();
        require(!f.manager.PendingAlphaInput.empty() && f.manager.PendingAlphaInput.front()->presentation==chosen,
                "budget retry discarded F while releasing completed candidate storage");
        require(alphaBudget->active==0,"completed candidates did not free their read allocations for the missing candidate");
        // A newly arriving event must not fill the budget before the old
        // incomplete frame's missing candidate has had a chance to encode.
        f.manager.PrimaryMouseMove(50,68,1);
        for(auto& image:current) ++image.texture.version;
        composeAlpha(f.manager,101);
        require(alphaBudget->active==2 && alphaBudget->peak==2,"FIFO supplemented reads exceeded/lost the bounded budget");
        completeAlphaRequests(); f.manager.ProcessPendingAlphaInput();
        require(f.primary.downs==1 && f.primary.eventX==35 && f.manager.PendingAlphaInput.size()==1,
                "new arrivals starved the old event or it used a newer transparent alpha/mapping");
        f.primary.MainImage=&next[0]; f.other.MainImage=&next[1]; third.MainImage=&next[2];
        for(auto& image:next) { image.texture.alpha=64; ++image.texture.version; }
        composeAlpha(f.manager,102);
        completeAlphaRequests(); f.manager.ProcessPendingAlphaInput();
        require(f.manager.PendingAlphaInput.empty(),"bounded FIFO recovery failed to finish the following event");
        require(source[0].texture.refs==1 && source[1].texture.refs==1 && source[2].texture.refs==1,
                "consumed event retained immutable source texture epochs");
        require(alphaBudget->active==0 && source[0].texture.syncReads==0 && current[0].texture.syncReads==0,
                "bounded read recovery leaked allocation or synchronously read alpha");
        completeEncodedAlpha=true; alphaBudget->limit=size_t(-1);
    }
    SDL_SetHint("MIKAGE_EMOTE_ASYNC_ALPHA","0");
}

int main() {
    try {
        trace::SetEnabled(true);
        propertyCallbacks();
        reentryAndExceptions();
        realInputStillQueries();
        builtInLifecycle<tTVPBasicDrawDevice>();
        builtInLifecycle<DrawDeviceD3D>();
        builtInLifecycle<D3D>();
        foreignSubclass<ForeignBasic>();
        foreignSubclass<ForeignD3D>();
        foreignSubclass<ForeignScriptD3D>();
        foreignWindow<tTVPBasicDrawDevice>();
        foreignWindow<DrawDeviceD3D>();
        foreignWindow<D3D>();
        ownerChangesReset();
        multipleManagers();
        asyncInputChain();
        std::cout << "PASS: property callbacks, input/capture, exceptions and device lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
