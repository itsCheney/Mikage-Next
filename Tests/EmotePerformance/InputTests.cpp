#include "AsyncAlphaTileCache.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace krkrsdl3;
static int checks=0;
static void Require(bool condition,const char* text) { ++checks; if(!condition) throw std::runtime_error(text); }
static auto Ticket(uint64_t serial,bool shown=true) {
    auto ticket=std::make_shared<AsyncLayerPresentation>(serial);
    ticket->presented.store(shown); return ticket;
}
static void Encode(AsyncAlphaTileCache& cache,uint64_t version,const std::shared_ptr<AsyncLayerPresentation>& ticket,
                   uint8_t alpha,bool completed=true) {
    cache.EncodeDemanded(version,ticket,[=](const auto& read) {
        const int w=read->region.Width(),h=read->region.Height();
        read->pitch=(w*4+255)&~255;
        read->rgba.assign(size_t(read->pitch)*h,0);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x)
            read->rgba[size_t(y)*read->pitch+x*4+3]=alpha;
        read->completed.store(completed); return true;
    });
}
static void AllAlphaAndBorders() {
    for(int value=0;value<256;++value) {
        AsyncAlphaTileCache cache; auto tile=cache.Demand(34,36,35,37);
        Require(tile && tile->region.Width()==3 && tile->region.Height()==5,"edge tile was not clipped");
        Encode(cache,1,Ticket(1),uint8_t(value));
        uint8_t alpha=0; std::shared_ptr<AsyncAlphaTileSnapshot> sample;
        Require(tile->Sample(34,36,alpha,sample) && alpha==value,"RGBA to R8 alpha changed a byte");
        Require(!sample->read && sample->alpha.size()==15,"materialized snapshot kept RGBA/GPU lease");
        Require(!tile->Sample(35,36,alpha,sample),"sample outside clipped tile succeeded");
    }
}
static void PresentationAndOwnership() {
    AsyncAlphaTileCache cache; auto tile=cache.Demand(2,3,64,64); auto ticket=Ticket(10,false);
    Encode(cache,1,ticket,17,false);
    auto request=tile->frames.back()->read;
    struct Lease { int* releases; ~Lease(){++*releases;} };
    int releases=0; request->allocationLease=std::make_shared<Lease>(Lease{&releases}); releases=0;
    Require(!tile->Latest(),"GPU unfinished bytes were published");
    request->completed.store(true);
    Require(!tile->Latest(),"GPU completion was confused with actual presentation");
    ticket->presented.store(true);
    auto snapshot=tile->Latest(); Require(snapshot && snapshot->alpha[0]==17,"shown completed tile stayed pending");
    Require(releases==1 && request->rgba.empty(),"consumed snapshot did not release storage held by a callback");
    request.reset(); Require(releases==1,"staging lease was released twice");
    auto next=Ticket(11,false); Encode(cache,1,next,99);
    Require(!tile->AtFrame(11),"static pixels published before their new frame was displayed");
    next->presented.store(true);
    Require(tile->AtFrame(11) && tile->AtFrame(11)->alpha[0]==17,"static reuse changed pixels/version");
    Require(tile->AtFrame(10)!=tile->AtFrame(11),"static layer did not retain distinct display tickets");
    next->failed.store(true); Require(!tile->AtFrame(11),"dropped presentation remained visible");
}
static void BoundsAndCancellation() {
    AsyncAlphaTileCache cache; std::vector<std::shared_ptr<AsyncAlphaTile>> pins;
    for(int i=0;i<32;++i) pins.push_back(cache.Demand(i*32,0,1088,32));
    Require(cache.TileCount()==32 && !cache.Demand(1056,0,1088,32),"pending event tile budget was unbounded/evicted pins");
    pins.erase(pins.begin()); Require(bool(cache.Demand(1056,0,1088,32)),"unpinned LRU tile did not free capacity");
    AsyncAlphaTileCache pending; auto tile=pending.Demand(0,0,32,32);
    for(uint64_t serial=1;serial<8;++serial) Encode(pending,serial,Ticket(serial),255,false);
    Require(tile->frames.size()==3,"in-flight per-tile budget exceeded three frames");
    auto read=tile->frames.front()->read; pending.Cancel();
    Require(read->canceled.load() && !read->Ready(),"texture destruction exposed canceled bytes");
    AsyncAlphaTileCache idle; auto idleTile=idle.Demand(0,0,32,32); idle.StopDemand();
    int encodes=0; idle.EncodeDemanded(1,Ticket(1),[&](const auto&){++encodes;return true;});
    Require(encodes==0 && idleTile->frames.empty(),"idle pointer kept requesting GPU readbacks");
    Require(!idle.Demand(-1,0,32,32) && !idle.Demand(32,0,32,32),"out-of-texture point demanded a tile");
}
int main() {
    try { AllAlphaAndBorders(); PresentationAndOwnership(); BoundsAndCancellation();
        std::cout << "PASS: " << checks << " production async alpha cache checks\n"; return 0;
    } catch(const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
