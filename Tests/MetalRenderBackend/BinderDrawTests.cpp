// Portable control-flow fixtures: actual production methods are extracted by
// extract-binder-draw.py. These assertions do not establish native GPU timing.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using tjs_int = int;
enum tTVPLayerType { ltBinder, ltEffect, ltFilter, ltAlpha, ltOpaque };
struct tTVPRect {
    int left=0, top=0, right=0, bottom=0;
    tTVPRect()=default;
    tTVPRect(int l,int t,int r,int b):left(l),top(t),right(r),bottom(b){}
    int get_width() const { return right-left; }
    int get_height() const { return bottom-top; }
    bool is_empty() const { return left>=right || top>=bottom; }
    void clear() { left=top=right=bottom=0; }
    void add_offsets(int x,int y) { left+=x; right+=x; top+=y; bottom+=y; }
    void set_offsets(int x,int y) { add_offsets(x-left,y-top); }
    bool operator==(const tTVPRect& r) const {
        return left==r.left && top==r.top && right==r.right && bottom==r.bottom;
    }
};
bool TVPIntersectRect(tTVPRect* out,const tTVPRect& a,const tTVPRect& b) {
    *out={std::max(a.left,b.left),std::max(a.top,b.top),
          std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
    return !out->is_empty();
}
struct tTVPComplexRect {
    std::vector<tTVPRect> rects;
    struct tIterator {
        const std::vector<tTVPRect>* rects=nullptr; size_t next=0;
        bool Step() { return rects && next<rects->size() ? (++next,true) : false; }
        const tTVPRect& operator*() const { return (*rects)[next-1]; }
    };
    tIterator GetIterator() const { return {&rects,0}; }
    void Clear() { rects.clear(); }
    void Or(const tTVPRect& r) { if(!r.is_empty()) rects.push_back(r); }
    void Sub(const tTVPRect& cut) {
        std::vector<tTVPRect> result;
        for(const auto& r:rects) {
            tTVPRect common;
            if(!TVPIntersectRect(&common,r,cut)) { result.push_back(r); continue; }
            for(const tTVPRect piece:{tTVPRect(r.left,r.top,r.right,common.top),
                    tTVPRect(r.left,common.bottom,r.right,r.bottom),
                    tTVPRect(r.left,common.top,common.left,common.bottom),
                    tTVPRect(common.right,common.top,r.right,common.bottom)})
                if(!piece.is_empty()) result.push_back(piece);
        }
        rects=std::move(result);
    }
    void Sub(const tTVPComplexRect& cuts) { for(const auto& r:cuts.rects) Sub(r); }
    int GetCount() const { return int(rects.size()); }
    void Unite() {}
};
constexpr int TVP_CACHE_UNITE_LIMIT=1000;
struct tTVPBaseTexture {
    int fills=0,copies=0;
    void Fill(const tTVPRect&,int) { ++fills; }
    void CopyRect(int,int,tTVPBaseTexture*,const tTVPRect&) { ++copies; }
    tTVPBaseTexture* GetTexture() { return this; }
};
using iTVPBaseBitmap=tTVPBaseTexture;
struct tTVPTempBitmapHolder {
    static inline int gets=0,frees=0,level=0,maxLevel=0;
    static inline std::vector<std::unique_ptr<tTVPBaseTexture>> slots;
    static tTVPBaseTexture* GetTemp(int,int) {
        ++gets; ++level; maxLevel=std::max(maxLevel,level);
        if(size_t(level)>slots.size()) slots.emplace_back(new tTVPBaseTexture);
        return slots[level-1].get();
    }
    static void FreeTemp() {
        if(level<=0) throw std::runtime_error("unbalanced FreeTemp");
        ++frees; --level;
    }
    static int Fills() { int n=0; for(const auto& p:slots) n+=p->fills; return n; }
    static void Reset() { gets=frees=level=maxLevel=0; slots.clear(); }
};
struct tTVPDrawable {
    virtual ~tTVPDrawable()=default;
    virtual tTVPBaseTexture* GetDrawTargetBitmap(const tTVPRect&,tTVPRect&)=0;
    virtual void DrawCompleted(const tTVPRect&,tTVPBaseTexture*,const tTVPRect&,tTVPLayerType,int)=0;
};
class tTJSNI_BaseLayer;
struct ManagerFixture {
    int invalidations=0;
    void InvalidateOverallIndex() { ++invalidations; }
};
template<class T> struct ChildList {
    std::vector<T*> items;
    int GetSafeLockedObjectCount() const { return int(items.size()); }
    T* GetSafeLockedObjectAt(int n) const { return items[n]; }
};
template<class T> struct tObjectListSafeLockHolder {
    explicit tObjectListSafeLockHolder(ChildList<T>&) {}
};
namespace krkrsdl3 { namespace layer_hotspot {
struct Scope { Scope(uint64_t,const char*) {} };
}}
constexpr int TVPInternalError=1;
void TVPThrowExceptionMessage(int) { throw std::runtime_error("invalid child target"); }
struct TransitionDrawable : tTVPDrawable {
    tTVPDrawable* target=nullptr;
    void Init(tTJSNI_BaseLayer*,tTVPDrawable* t) { target=t; }
    tTVPBaseTexture* GetDrawTargetBitmap(const tTVPRect& r,tTVPRect& c) override {
        return target->GetDrawTargetBitmap(r,c);
    }
    void DrawCompleted(const tTVPRect& r,tTVPBaseTexture* b,const tTVPRect& c,tTVPLayerType t,int o) override {
        target->DrawCompleted(r,b,c,t,o);
    }
};
class tTJSNI_BaseLayer : public tTVPDrawable {
public:
    uint64_t LayerDiagnosticID=1;
    tTVPLayerType Type=ltBinder,DisplayType=ltBinder;
    int Opacity=255,ImageLeft=0,ImageTop=0,NeutralColor=1,TransparentColor=0;
    bool Visible=true,InTransition=false,TransWithChildren=false,DirectTransferToParent=false;
    bool ChildrenArrayValid=true,ChildrenOrderIndexValid=true;
    tTJSNI_BaseLayer *Parent=nullptr,*TransSrc=nullptr,*TransDest=nullptr;
    void *DivisibleTransHandler=nullptr,*GiveUpdateTransHandler=nullptr;
    int CacheEnabledCount=0,cacheAllocations=0,transitionCopies=0,blends=0;
    tTVPBaseTexture *MainImage=nullptr,*CacheBitmap=nullptr,*UpdateBitmapForChild=nullptr;
    tTVPBaseTexture cacheStorage;
    ManagerFixture* Manager=nullptr;
    tTVPDrawable* CurrentDrawTarget=nullptr;
    TransitionDrawable TransDrawable;
    tTVPRect Rect{30,40,130,120},UpdateExcludeRect,UpdateRectForChild;
    int UpdateRectForChildOfsX=0,UpdateRectForChildOfsY=0,UpdateOfsX=0,UpdateOfsY=0;
    tTVPComplexRect DrawnRegion,CacheRecalcRegion,overlapped,exposed;
    ChildList<tTJSNI_BaseLayer> Children;
    bool GetCacheEnabled() const { return CacheEnabledCount!=0; }
    void EnsureCacheAllocated() { ++cacheAllocations; CacheBitmap=&cacheStorage; }
    bool IsSeen() const { return Visible; }
    void ParentRectToChildRect(tTVPRect& r) { r.add_offsets(-Rect.left,-Rect.top); }
    int GetVisibleChildrenCount() const {
        return int(std::count_if(Children.items.begin(),Children.items.end(),
                    [](auto* c){return c && c->Visible;}));
    }
    const tTVPComplexRect& GetOverlappedRegion() { return overlapped; }
    const tTVPComplexRect& GetExposedRegion() { return exposed; }
    void DoDivisibleTransition(tTVPBaseTexture*,int,int,const tTVPRect&) { ++transitionCopies; }
    void BltImage(tTVPBaseTexture*,tTVPLayerType,int,int,tTVPBaseTexture*,const tTVPRect&,tTVPLayerType,int) { ++blends; }
    void DrawSelf(tTVPDrawable*,tTVPRect&,tTVPRect&);
    void CopySelfForRect(iTVPBaseBitmap*,int,int,const tTVPRect&);
    void CopySelf(iTVPBaseBitmap*,int,int,const tTVPRect&);
    void InternalDrawNoCache_CPU(tTVPDrawable*,const tTVPRect&);
    virtual void Draw(tTVPDrawable*,const tTVPRect&,bool);
    tTVPBaseTexture* GetDrawTargetBitmap(const tTVPRect&,tTVPRect&) override;
    void DrawCompleted(const tTVPRect&,tTVPBaseTexture*,const tTVPRect&,tTVPLayerType,int) override;
};
#include "ProductionBinderDraw.inc"

void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct Completion {
    tTVPRect dest,clip; tTVPBaseTexture* bitmap; tTVPLayerType type; int opacity;
};
struct Target : tTVPDrawable {
    tTVPBaseTexture image;
    std::vector<tTVPRect> queries;
    std::vector<Completion> completions;
    tTVPBaseTexture* GetDrawTargetBitmap(const tTVPRect& r,tTVPRect& clip) override {
        queries.push_back(r); clip=r; clip.add_offsets(7,11); return &image;
    }
    void DrawCompleted(const tTVPRect& r,tTVPBaseTexture* b,const tTVPRect& c,tTVPLayerType t,int o) override {
        completions.push_back({r,c,b,t,o});
    }
};
struct Leaf : tTJSNI_BaseLayer {
    int id=0; std::vector<int>* order=nullptr;
    bool fail=false,query=true;
    std::vector<tTVPRect> draws;
    explicit Leaf(int n=0,std::vector<int>* sequence=nullptr):id(n),order(sequence) {
        Rect={0,0,100,80};
    }
    void Draw(tTVPDrawable* target,const tTVPRect& r,bool) override {
        if(order) order->push_back(id);
        draws.push_back(r);
        if(fail) throw std::runtime_error("child draw failure");
        tTVPRect clip=r;
        auto* b=query ? target->GetDrawTargetBitmap(r,clip) : &cacheStorage;
        target->DrawCompleted(r,b,clip,ltAlpha,137);
    }
};
void Run(tTJSNI_BaseLayer& layer,Target& target,bool cpu,const tTVPRect& local) {
    if(cpu) {
        layer.CurrentDrawTarget=&target;
        layer.InternalDrawNoCache_CPU(&target,local);
    } else {
        auto parent=local; parent.add_offsets(layer.Rect.left,layer.Rect.top);
        layer.Draw(&target,parent,true);
        Require(layer.CurrentDrawTarget==nullptr,"Draw clears target after success");
    }
}

void SafeBinderCases() {
    for(bool cpu:{false,true}) for(auto type:{ltBinder,ltEffect,ltFilter}) {
        tTVPTempBitmapHolder::Reset();
        std::vector<int> order; Leaf first(1,&order),second(2,&order),hidden(3,&order),outside(4,&order);
        first.Rect={0,0,60,70}; second.Rect={20,10,100,80};
        hidden.Visible=false; outside.Rect={200,200,210,210};
        tTJSNI_BaseLayer binder; binder.Type=type; binder.Opacity=93;
        binder.Children.items={&first,nullptr,&hidden,&second,&outside};
        binder.overlapped.rects={{0,0,80,65},{90,0,100,5}};
        binder.exposed.rects={{0,65,100,80}};
        ManagerFixture manager; binder.Manager=&manager;
        Target target;
        Run(binder,target,cpu,{10,5,90,75});
        Require(tTVPTempBitmapHolder::gets==0 && tTVPTempBitmapHolder::frees==0 &&
                tTVPTempBitmapHolder::Fills()==0,"safe binder performs no temporary allocation/fill");
        Require(order==std::vector<int>({1,2,1,2}),"clipped overlap/exposed child order");
        Require(first.draws[0]==tTVPRect(10,5,60,65) && second.draws[0]==tTVPRect(20,10,80,65),
                "overlap child clipping unchanged");
        Require(first.draws[1]==tTVPRect(10,65,60,70) && second.draws[1]==tTVPRect(20,65,90,75),
                "exposed child clipping unchanged");
        Require(target.queries[0]==tTVPRect(40,45,90,105) &&
                target.completions[0].dest==target.queries[0] &&
                target.completions[0].clip==tTVPRect(47,56,97,116),
                "binder forwards parent coordinates and parent clip unchanged");
        for(const auto& completion:target.completions)
            Require(completion.bitmap==&target.image && completion.type==ltAlpha && completion.opacity==137,
                    "binder preserves child buffer/type/opacity");
        Require(!binder.DirectTransferToParent && !binder.ChildrenArrayValid && !binder.ChildrenOrderIndexValid,
                "traversal bookkeeping preserved");
        // A second frame must not leave a stale temporary/offset or unbalanced level.
        Run(binder,target,cpu,{10,5,90,75});
        Require(tTVPTempBitmapHolder::gets==0,"repeat draw stays allocation-free");
    }
}

void UnsafeBinderCases() {
    for(bool cpu:{false,true}) for(int unsafe=0;unsafe<10;++unsafe) {
        tTVPTempBitmapHolder::Reset();
        tTJSNI_BaseLayer binder,participant;
        Leaf child; binder.Children.items={&child}; binder.overlapped.Or({0,0,80,60});
        tTVPBaseTexture image;
        switch(unsafe) {
            case 0: binder.InTransition=true; break;
            case 1: binder.TransSrc=&participant; break;
            case 2: binder.TransDest=&participant; break;
            case 3: binder.DivisibleTransHandler=&participant; break;
            case 4: binder.GiveUpdateTransHandler=&participant; break;
            case 5: binder.MainImage=&image; break;
            case 6: binder.Type=ltAlpha; break;
            case 7: binder.DisplayType=ltAlpha; break;
            case 8: binder.CacheEnabledCount=1; binder.InTransition=true;
                    binder.DivisibleTransHandler=&participant; break;
            case 9: binder.InTransition=true; binder.TransWithChildren=true; break;
        }
        Target target; Run(binder,target,cpu,{0,0,80,60});
        Require(tTVPTempBitmapHolder::gets==1 && tTVPTempBitmapHolder::frees==1 &&
                tTVPTempBitmapHolder::level==0,"unsafe binder retains balanced temporary path");
        if(unsafe!=5 && unsafe!=8) Require(tTVPTempBitmapHolder::Fills()==1,"unsafe binder transparent fill retained");
        if(unsafe==5) Require(image.fills==0 && tTVPTempBitmapHolder::slots[0]->copies==1,
                              "unexpected image retains copy");
        if(unsafe==8) Require(binder.transitionCopies==1,"transition-without-children keeps transition copy");
    }
    tTVPTempBitmapHolder::Reset();
    tTJSNI_BaseLayer cached; Leaf child;
    cached.CacheEnabledCount=1; cached.CacheRecalcRegion.Or({0,0,80,60}); cached.Children.items={&child};
    Target target; Run(cached,target,false,{0,0,80,60});
    Require(cached.cacheAllocations==1 && target.completions.back().bitmap==cached.CacheBitmap &&
            cached.CacheBitmap->fills==1,"binder cache materialization/fill/completion retained");
    tTVPTempBitmapHolder::Reset();
    cached.overlapped.Or({0,0,80,60});
    Run(cached,target,true,{0,0,80,60});
    Require(tTVPTempBitmapHolder::gets==1 && tTVPTempBitmapHolder::Fills()==1,
            "CPU path excludes cached binder even when invoked directly");
}

void OrdinaryAndNestedCases() {
    for(bool cpu:{false,true}) for(bool opaque:{false,true}) {
        tTVPTempBitmapHolder::Reset();
        tTJSNI_BaseLayer layer; layer.Type=layer.DisplayType=opaque ? ltOpaque:ltAlpha;
        tTVPBaseTexture image; layer.MainImage=&image;
        Leaf child; layer.Children.items={&child}; layer.overlapped.Or({5,7,80,60});
        Target target; Run(layer,target,cpu,{0,0,90,70});
        Require(tTVPTempBitmapHolder::gets==(opaque?0:1) && tTVPTempBitmapHolder::level==0,
                "ordinary layer keeps opaque/direct or alpha/temporary allocation");
        Require(target.completions.size()==1 && target.completions[0].dest==tTVPRect(35,47,110,100) &&
                target.completions[0].type==layer.DisplayType,"ordinary layer completion retained");
    }
    tTVPTempBitmapHolder::Reset();
    tTJSNI_BaseLayer parent,binder,grandchild;
    parent.Type=parent.DisplayType=ltAlpha; parent.overlapped.Or({0,0,90,70});
    binder.Rect={10,12,80,65}; binder.overlapped.Or({0,0,70,53});
    grandchild.Type=grandchild.DisplayType=ltAlpha;
    grandchild.Rect={4,6,54,46}; grandchild.overlapped.Or({0,0,50,40});
    tTVPBaseTexture parentImage,childImage;
    parent.MainImage=&parentImage; grandchild.MainImage=&childImage;
    Leaf leaf; leaf.Rect={0,0,50,40};
    parent.Children.items={&binder}; binder.Children.items={&grandchild}; grandchild.Children.items={&leaf};
    Target target; Run(parent,target,false,{0,0,90,70});
    Require(tTVPTempBitmapHolder::gets==2 && tTVPTempBitmapHolder::frees==2 &&
            tTVPTempBitmapHolder::maxLevel==2 && tTVPTempBitmapHolder::level==0,
            "nested binder removes only unused pool level, preserves ancestor and descendant temporaries");
    Require(tTVPTempBitmapHolder::slots[0].get()!=tTVPTempBitmapHolder::slots[1].get() && parent.blends==1,
            "nested child temporary does not alias live parent composition");
    // Exercise binder-to-binder coordinate forwarding all the way to the target.
    tTVPTempBitmapHolder::Reset();
    tTJSNI_BaseLayer outerBinder,innerBinder;
    outerBinder.overlapped.Or({0,0,90,70}); outerBinder.Children.items={&innerBinder};
    innerBinder.Rect={8,9,68,59}; innerBinder.overlapped.Or({0,0,60,50});
    Leaf nestedLeaf; nestedLeaf.Rect={0,0,60,50}; innerBinder.Children.items={&nestedLeaf};
    Target nestedTarget; Run(outerBinder,nestedTarget,false,{0,0,90,70});
    Require(tTVPTempBitmapHolder::gets==0 && nestedTarget.queries[0]==tTVPRect(38,49,98,99) &&
            nestedTarget.completions[0].dest==nestedTarget.queries[0],
            "nested binders forward accumulated offsets without allocating a temporary");
    for(bool cpu:{false,true}) {
        tTVPTempBitmapHolder::Reset();
        tTJSNI_BaseLayer empty; ManagerFixture manager; empty.Manager=&manager;
        Target emptyTarget; Run(empty,emptyTarget,cpu,{0,0,90,70});
        Require(tTVPTempBitmapHolder::gets==0 && emptyTarget.completions.empty(),
                "empty binder performs no image work");
    }
    for(bool safe:{false,true}) for(bool cpu:{false,true}) {
        tTVPTempBitmapHolder::Reset();
        // Hold an outer buffer to verify a throwing child cannot release its level.
        auto* outer=tTVPTempBitmapHolder::GetTemp(100,100);
        tTJSNI_BaseLayer binder; if(!safe) binder.TransDest=&parent;
        Leaf child; child.fail=true; binder.Children.items={&child}; binder.overlapped.Or({0,0,80,60});
        Target failureTarget;
        bool threw=false;
        try { Run(binder,failureTarget,cpu,{0,0,80,60}); } catch(const std::runtime_error&) { threw=true; }
        Require(threw && tTVPTempBitmapHolder::level==1 && tTVPTempBitmapHolder::slots[0].get()==outer,
                "throwing child retains outer temporary level");
        Require(tTVPTempBitmapHolder::gets==(safe?1:2) && tTVPTempBitmapHolder::frees==(safe?0:1),
                "throwing safe/unsafe binder frees only its own temporary");
        tTVPTempBitmapHolder::FreeTemp();
    }
}
} // namespace

void BinderDrawTests() {
    SafeBinderCases(); UnsafeBinderCases(); OrdinaryAndNestedCases();
}

#ifdef BINDER_DRAW_TEST_STANDALONE
int main() {
    try { BinderDrawTests(); std::cout << "binder draw fixtures passed\n"; return 0; }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#endif
