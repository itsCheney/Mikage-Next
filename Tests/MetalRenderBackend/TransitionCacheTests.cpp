#include "backend/TransitionCache.h"
#include "LayerHotspotContext.h"
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

using tjs_uint=unsigned; using tjs_int=int;
struct tTVPRect {
    int left=0,top=0,right=16,bottom=12;
    tTVPRect()=default;
    tTVPRect(int l,int t,int r,int b):left(l),top(t),right(r),bottom(b){}
    int get_width() const {return right-left;} int get_height() const {return bottom-top;}
};
struct Region { bool dirty=false; void Or(const tTVPRect&) {dirty=true;} };
struct tTVPBaseTexture {
    static int live,allocations; static bool fail;
    int width,height;
    tTVPBaseTexture(int w,int h,int):width(w),height(h) {
        if(fail) throw std::bad_alloc(); ++live;++allocations;
    }
    ~tTVPBaseTexture() {--live;}
    void SetSize(int w,int h) {width=w;height=h;}
    int GetWidth() const {return width;} int GetHeight() const {return height;}
};
int tTVPBaseTexture::live=0,tTVPBaseTexture::allocations=0;bool tTVPBaseTexture::fail=false;
namespace krkrsdl3 {
struct FixtureBackend { const char* name="metal"; const char* GetName() const {return name;} };
static FixtureBackend fixtureBackend;
static FixtureBackend* TVPGetRenderBackend() {return &fixtureBackend;}
}
static bool nativeComposition=true;
static bool TVPMetalLayerCompositionActive() {return nativeComposition;}
static bool TVPFreeUnusedLayerCache=false;
struct Object {int references=1;void AddRef(){++references;} void Release(){--references;}};
struct Handler {int references=1;void Release(){--references;}};
struct tTJSVariantClosure {tTJSVariantClosure(void*,void*){} void Release(){}};
struct tTJSVariant {tTJSVariant()=default;tTJSVariant(Object*,Object*){}}
;
using ttstr=std::string;
#define TJS_N(x) x
constexpr int ttSimple=0,ttExchange=1,TVP_EPT_IMMEDIATE=0;
static int removedHooks=0,eventCount=0;
static std::function<void()> synchronousEvent;
static void TVPRemoveContinuousEventHook(void*) {++removedHooks;}
static void TVPPostEvent(Object*,Object*,const ttstr&,int,int,int,tTJSVariant*) {
    ++eventCount; if(synchronousEvent) synchronousEvent();
}
class tTJSNI_BaseLayer {
public:
    uint64_t LayerDiagnosticID=1;
    tTVPRect Rect;
    tTVPBaseTexture *CacheBitmap=nullptr,*MainImage=nullptr;
    bool TransitionCachePending=false,Cached=false;
    tjs_uint CacheEnabledCount=0;
    Region CacheRecalcRegion;
    int compactHooks=0,exchanges=0,swaps=0;
    bool InTransition=false,TransCompEventPrevented=false,TransSelfUpdate=false,TransWithChildren=true;
    bool Shutdown=false,Visible=true;
    int TransType=ttSimple,TransIdleCallback=0;
    tTJSNI_BaseLayer *TransSrc=nullptr,*TransDest=nullptr;
    Object *Owner=nullptr,*TransDestObj=nullptr,*TransSrcObj=nullptr;
    Handler *DivisibleTransHandler=nullptr,*GiveUpdateTransHandler=nullptr;
    tTJSVariantClosure TransTickCallback{nullptr,nullptr};
    ~tTJSNI_BaseLayer(){delete CacheBitmap;}
    void RegisterCompactEventHook(){++compactHooks;}
    void RecordTransitionLifecycle(const char*){}
    bool GetVisible() const{return Visible;} void SetVisible(bool b){Visible=b;}
    void SetPosition(int l,int t){Rect.left=l;Rect.top=t;}
    void Exchange(tTJSNI_BaseLayer*){++exchanges;} void Swap(tTJSNI_BaseLayer*){++swaps;}
    void AllocateCache();void EnsureCacheAllocated();tjs_uint IncTransitionCacheEnabledCount();
    void ResizeCache();void DeallocateCache();void CompactCache();
    tjs_uint IncCacheEnabledCount();tjs_uint DecCacheEnabledCount();void SetCached(bool);
    void InternalStopTransition();
};
#include "ProductionTransitionCache.inc"

void TransitionCacheTests() {
    const auto require=[](bool ok) {if(!ok) throw std::runtime_error("transition cache lifecycle");};
    using krkrsdl3::transition_cache::Snapshot;
    const auto start=Snapshot();
    {
        tTJSNI_BaseLayer layer;
        layer.IncTransitionCacheEnabledCount();
        require(layer.CacheEnabledCount==1 && layer.TransitionCachePending && !layer.CacheBitmap);
        require(layer.compactHooks==1 && layer.CacheRecalcRegion.dirty);
        layer.Rect.right=30;layer.ResizeCache();require(!layer.CacheBitmap);
        tTVPBaseTexture::fail=true;
        try {layer.EnsureCacheAllocated();require(false);} catch(const std::bad_alloc&){}
        require(layer.CacheEnabledCount==1 && layer.TransitionCachePending && !layer.CacheBitmap);
        try {layer.IncCacheEnabledCount();require(false);} catch(const std::bad_alloc&){}
        require(layer.CacheEnabledCount==1 && layer.TransitionCachePending && !layer.CacheBitmap);
        tTVPBaseTexture::fail=false;
        layer.EnsureCacheAllocated();require(layer.CacheBitmap && layer.CacheBitmap->width==30);
        require(!layer.TransitionCachePending && layer.CacheEnabledCount==1);
        layer.IncCacheEnabledCount();require(layer.CacheEnabledCount==2);
        layer.DecCacheEnabledCount();layer.DecCacheEnabledCount();
        layer.IncTransitionCacheEnabledCount();require(!layer.TransitionCachePending); // existing cache
        layer.DecCacheEnabledCount();layer.CompactCache();require(!layer.CacheBitmap);
    }
    {
        tTJSNI_BaseLayer destination,source;
        Object owner,srcOwner;Handler handler;
        destination.Owner=&owner;source.Owner=&srcOwner;
        destination.IncTransitionCacheEnabledCount();source.IncTransitionCacheEnabledCount();
        destination.InTransition=true;destination.TransSrc=&source;source.TransDest=&destination;
        destination.TransDestObj=&owner;destination.TransSrcObj=&srcOwner;
        owner.AddRef();srcOwner.AddRef();destination.DivisibleTransHandler=&handler;
        destination.TransType=ttExchange;source.Rect.left=40;destination.Rect.left=9;
        const int beforeAlloc=tTVPBaseTexture::allocations;
        synchronousEvent=[&]{destination.IncTransitionCacheEnabledCount();};
        destination.InternalStopTransition();synchronousEvent={};
        require(destination.exchanges==1 && !destination.swaps && destination.Rect.left==40);
        require(source.Rect.left==9 && !source.TransDest && !destination.TransSrc);
        require(destination.CacheEnabledCount==1 && destination.TransitionCachePending);
        require(source.CacheEnabledCount==0 && !source.TransitionCachePending);
        require(tTVPBaseTexture::allocations==beforeAlloc && handler.references==0);
        require(owner.references==1 && srcOwner.references==1 && eventCount==1 && removedHooks==1);
        destination.DecCacheEnabledCount();require(!destination.TransitionCachePending);
        destination.CompactCache();require(!destination.CacheBitmap);
    }
    for(int backendCase=0;backendCase<3;++backendCase) {
        tTJSNI_BaseLayer layer;
        krkrsdl3::fixtureBackend.name=backendCase==0 ? "software":"metal";
        nativeComposition=backendCase!=1;
        krkrsdl3::transition_cache::SetEnabled(backendCase!=2);
        layer.IncTransitionCacheEnabledCount();require(layer.CacheBitmap && !layer.TransitionCachePending);
        layer.DecCacheEnabledCount();
    }
    krkrsdl3::transition_cache::SetEnabled(true);nativeComposition=true;
    const auto end=Snapshot();
    require(end.eligible-start.eligible==4 && end.materialized-start.materialized==1 && end.cancelled-start.cancelled==3);
    require(tTVPBaseTexture::live==0);
}
