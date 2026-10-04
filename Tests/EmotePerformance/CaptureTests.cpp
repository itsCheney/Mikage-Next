#include "emotecapturecache.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

using namespace emoteplayer::performance;
static int checks = 0;
static void check(bool condition,const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static bool equal(Rect a,Rect b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
static TextureKey key(std::uint64_t id,std::uint64_t version) { return {id,version,true}; }
static Bounds box(int x,int y,int right,int bottom) { return {true,{x,y,right,bottom}}; }
static void frame(CanvasCaptureCache& canvas,DrawToken token,Bounds bounds) {
    canvas.beginClear(); canvas.recordDraw(token,bounds);
}
static CaptureDecision publish(CanvasCaptureCache& canvas,std::uintptr_t layer,TextureKey before,TextureKey after) {
    const auto decision=canvas.decide(layer,before);
    canvas.commit(layer,after,decision.sourceVersion);
    return decision;
}

int main() {
    CanvasCaptureCache canvas;
    canvas.reset(200,100);
    auto first=canvas.decide(1,key(11,0));
    check(!first.skip && !first.canRegionCopy,"first capture must populate the entire destination");
    canvas.commit(1,key(11,1),first.sourceVersion);
    check(canvas.decide(1,key(11,1)).skip,"repeated empty capture can skip");
    frame(canvas,{7,1},box(20,20,60,60));
    auto initial=publish(canvas,1,key(11,1),key(11,2));
    check(initial.canRegionCopy && equal(initial.update,{20,20,60,60}),"empty to visible only changes new bounds");
    check(canvas.canRetainSingleDraw({7,1}),"private self-cleared single drawing may be retained");
    check(!canvas.canRetainSingleDraw({7,2}),"different pose cannot retain target");
    canvas.beginClear();
    check(!canvas.canRetainSingleDraw({7,1}),"explicit clear invalidates private retained drawing before a new draw");
    frame(canvas,{7,1},box(20,20,60,60));
    const auto repeat=canvas.decide(1,key(11,2));
    check(repeat.skip && repeat.sourceVersion==initial.sourceVersion,"equal ordered recipes keep a content version");
    check(!canvas.decide(2,key(11,2)).skip,"new layer destination requires its own capture");
    auto second=publish(canvas,2,key(12,1),key(12,2));
    check(!second.canRegionCopy,"uninitialized independent destination uses full copy");
    check(canvas.decide(2,key(12,2)).skip && canvas.decide(1,key(11,2)).skip,"each destination remembers its captured version");

    frame(canvas,{7,2},box(50,35,80,65));
    auto moved=canvas.decide(1,key(11,2));
    check(!moved.skip && moved.canRegionCopy && equal(moved.update,{20,20,80,65}),"moving/shrinking clears old union new bounds");
    check(equal(canvas.decide(1,key(11,2)).update,moved.update),"failed copy leaves old damage available for retry");
    check(canvas.decide(1,key(11,2)).sourceVersion==moved.sourceVersion,"retry does not manufacture a new source version");
    canvas.commit(1,key(11,3),moved.sourceVersion);
    auto dest2=canvas.decide(2,key(12,2));
    check(!dest2.skip && equal(dest2.update,{20,20,80,65}),"second destination still receives old-to-new damage");
    check(!canvas.decide(1,key(11,4)).canRegionCopy,"external write forces full destination overwrite");
    check(!canvas.decide(1,key(99,3)).skip,"replacement texture identity prevents skip");
    check(!canvas.decide(1,{}).skip,"unobservable destination never skips");
    canvas.commit(1,{},moved.sourceVersion);
    check(canvas.decide(1,key(11,3)).skip,"invalid destination key is not committed");

    canvas.beginClear();
    auto hidden=canvas.decide(1,key(11,3));
    check(hidden.canRegionCopy && equal(hidden.update,{50,35,80,65}),"hiding last player clears previous visible pixels");
    canvas.commit(1,key(11,4),hidden.sourceVersion);
    frame(canvas,{7,3},{});
    auto invisible=canvas.decide(1,key(11,4));
    check(invisible.canRegionCopy && invisible.update.empty(),"changed invisible pose has empty visible damage");

    canvas.beginClear(); canvas.recordDraw({7,4},box(10,10,30,30)); canvas.recordDraw({8,4},box(90,20,130,80));
    auto pair=publish(canvas,1,key(11,4),key(11,5));
    check(equal(canvas.bounds().rect,{10,10,130,80}),"shared canvas unions all player bounds");
    check(!canvas.canRetainSingleDraw({7,4}),"shared canvas cannot retain as a private drawing");
    canvas.beginClear(); canvas.recordDraw({7,4},box(10,10,30,30)); canvas.recordDraw({8,4},box(90,20,130,80));
    check(canvas.decide(1,key(11,5)).skip,"same pair and order can skip capture");
    canvas.beginClear(); canvas.recordDraw({8,4},box(90,20,130,80)); canvas.recordDraw({7,4},box(10,10,30,30));
    auto reorder=canvas.decide(1,key(11,5));
    check(!reorder.skip && reorder.sourceVersion!=pair.sourceVersion,"draw order changes composition version");
    canvas.commit(1,key(11,6),reorder.sourceVersion);
    frame(canvas,{7,4},box(10,10,30,30));
    auto removed=canvas.decide(1,key(11,6));
    check(equal(removed.update,{10,10,130,80}),"removing second player damages its old region");
    canvas.commit(1,key(11,7),removed.sourceVersion);
    canvas.recordDraw({7,4},box(10,10,30,30));
    check(!canvas.decide(1,key(11,7)).skip,"drawing without clear accumulates and cannot reuse the previous capture");

    frame(canvas,{7,5},{false,{}});
    auto gpu=canvas.decide(1,key(11,7));
    check(!gpu.canRegionCopy && equal(gpu.update,{0,0,200,100}),"unknown GPU deformation falls back to full damage");
    canvas.commit(1,key(11,8),gpu.sourceVersion);
    frame(canvas,{7,6},box(10,10,20,20));
    check(!canvas.decide(1,key(11,8)).canRegionCopy,"previous unknown bounds also require a full overwrite");
    auto known=publish(canvas,1,key(11,8),key(11,9));
    frame(canvas,{7,7},box(-10,-20,220,120));
    check(equal(canvas.decide(1,key(11,9)).update,{0,0,200,100}),"damage clips to canvas edges");
    canvas.invalidate();
    check(!canvas.decide(1,key(11,9)).canRegionCopy,"unknown canvas writer forces full capture");
    check(!canvas.canRetainSingleDraw({7,7}),"unknown writer invalidates render reuse");
    canvas.reset(200,100);
    check(!canvas.decide(1,key(11,9)).skip,"same-size target recreation invalidates destination history");
    canvas.reset(40,30);
    check(equal(canvas.decide(1,key(11,9)).update,{0,0,40,30}),"resize captures complete resized texture");

    // Simulate GPU region-write refusal for a shared/COW bitmap. Production's
    // CopyFromGPUTargetRegion rejects it before encoding and then full overwrite
    // must replace only the mutable destination, preserving the clone.
    CanvasCaptureCache cow;
    cow.reset(8,8); frame(cow,{1,1},box(1,1,3,3));
    auto before=publish(cow,1,key(80,0),key(80,1));
    (void)before;
    frame(cow,{1,2},box(4,4,6,6));
    const auto attempted=cow.decide(1,key(80,1));
    check(attempted.canRegionCopy,"COW source change retains damage decision before transaction check");
    cow.commit(1,key(81,1),attempted.sourceVersion); // full overwrite allocated a separate texture
    check(cow.decide(1,key(81,1)).skip,"commit follows actual post-COW texture identity");
    check(!cow.decide(2,key(80,1)).skip,"clone containing old pixels is never mistaken for the rewritten destination");

    CanvasCaptureCache bounded;
    bounded.reset(20,20);
    for (std::uintptr_t i=1;i<=17;++i) publish(bounded,i,key(i,0),key(i,1));
    check(!bounded.decide(1,key(1,1)).skip,"destination LRU eviction is safe full fallback");
    check(bounded.decide(17,key(17,1)).skip,"recent destination survives bounded cache eviction");
    bounded.beginClear();
    for (std::uint64_t i=0;i<257;++i) bounded.recordDraw({i,1},{});
    auto overflow=publish(bounded,20,key(20,0),key(20,1));
    bounded.beginClear();
    for (std::uint64_t i=0;i<257;++i) bounded.recordDraw({i,1},{});
    check(!bounded.decide(20,key(20,1)).skip,"overflowing recipe cannot accidentally match a truncated sequence");
    check(!bounded.canRetainSingleDraw({1,1}),"overflow recipe cannot retain render target");
    (void)overflow; (void)known;
    check(nextPlayerIdentity()!=nextPlayerIdentity(),"player recreation receives a distinct content identity");

    // Exercise old-union-new damage against a pixel oracle across moving,
    // disappearing, overlapping and reordered players. Each Layer keeps its
    // own older frame; a correct cache must not leave pixels from that frame.
    CanvasCaptureCache pixels;
    constexpr int w=40,h=30;
    pixels.reset(w,h);
    std::vector<int> destinations[2]={std::vector<int>(w*h,99),std::vector<int>(w*h,98)};
    std::uint64_t versions[2]={0,0};
    std::mt19937 random(0x43415054);
    std::uniform_int_distribution<int> coordinate(-10,45);
    for(std::uint64_t step=1;step<=200;++step) {
        pixels.beginClear();
        std::vector<int> source(w*h,0);
        const int count=step%7==0 ? 0 : 2;
        for(int order=0;order<count;++order) {
            const int player=step%2 ? order : 1-order;
            const int x=coordinate(random),y=coordinate(random);
            const Rect r{x,y,x+3+int(step%12),y+2+int(step%9)};
            pixels.recordDraw({std::uint64_t(player+1),step}, {true,r});
            const Rect c=clip(r,w,h);
            for(int row=c.top;row<c.bottom;++row) for(int col=c.left;col<c.right;++col)
                source[row*w+col]=int(step)*2+player+1;
        }
        if(step%19==0) pixels.invalidate(); // unknown write must use full copy
        for(int destination=0;destination<2;++destination) {
            if(destination==1 && step%3) continue; // deliberately old independent Layer
            const auto id=std::uintptr_t(destination+1);
            if(step%23==0) { destinations[destination][0]=-1; ++versions[destination]; }
            const auto action=pixels.decide(id,key(id,versions[destination]));
            if(!action.skip) {
                const Rect dirty=action.canRegionCopy ? action.update : Rect{0,0,w,h};
                for(int row=dirty.top;row<dirty.bottom;++row) for(int col=dirty.left;col<dirty.right;++col)
                    destinations[destination][row*w+col]=source[row*w+col];
                ++versions[destination];
                pixels.commit(id,key(id,versions[destination]),action.sourceVersion);
            }
            check(destinations[destination]==source,"region capture pixel oracle: no remnants across player/Layer changes");
            check(pixels.decide(id,key(id,versions[destination])).skip,"published capture can skip without changing a pixel");
        }
    }
    std::cout << "Capture: " << checks << " checks passed\n";
}
