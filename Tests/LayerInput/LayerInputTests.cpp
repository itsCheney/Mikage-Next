#include "PointReadTrace.h"
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
enum tTVPHitType { htMask, htProvince };
struct TestRect { int left=0,top=0,right=128,bottom=128;
    int get_width() const { return right-left; } int get_height() const { return bottom-top; } };
static uint64_t nextTextureID=1, nextLayerLifetimeID=1;
struct iTVPTexture2D {
    uint64_t identity=nextTextureID++, version=1;
    int width=128,height=128, syncReads=0;
    uint8_t alpha=255;
    bool gpu=true;
    int refs=1;
    void AddRef() { ++refs; } void Release() { --refs; }
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
    uint64_t LifetimeID = nextLayerLifetimeID++;
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
    // Production getters throw "Not drawable layer type" without a MainImage.
    int GetImageLeft() const {
        if(!MainImage) throw std::runtime_error("Not drawable layer type");
        return ImageLeft;
    }
    int GetImageTop() const {
        if(!MainImage) throw std::runtime_error("Not drawable layer type");
        return ImageTop;
    }
    void Update() { ++updates; }
    bool _HitTestNoVisibleCheck(int,int);
    iTJSDispatch2* GetOwnerNoAddRef() { return Owner; }
    uint64_t GetLifetimeID() const { return LifetimeID; }
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
        int localX=x,localY=y;
        hit->FromPrimaryCoordinates(localX,localY);
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
        std::cout << "PASS: property callbacks, input/capture, exceptions and device lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
