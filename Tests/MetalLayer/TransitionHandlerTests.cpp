// Compile and call the real seven plugin handlers. CPU Process is the oracle;
// the GPU fixture executes production MSL math through the active backend.
#include "tjsCommHead.h"
#include "TVPTrans.h"
#include "LayerTransition.h"
#include "TVPCompositor.h"
#include "cpu_types.h"
#include "mosaic.h"
#include "wave.h"
#include "ripple.h"
#include "turn.h"
#include "rotatetrans.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::map<ttstr, iTVPTransHandlerProvider *> providers;
void Check(bool value, const char *message) {
    if(!value) throw std::runtime_error(message);
}
class Options : public iTVPSimpleOptionProvider {
public:
    std::map<ttstr, tTJSVariant> values;
    tjs_error AddRef() override { return TJS_S_OK; }
    tjs_error Release() override { return TJS_S_OK; }
    tjs_error Reserved2() override { return TJS_E_NOTIMPL; }
    tjs_error GetDispatchObject(iTJSDispatch2 **out) override { *out=nullptr; return TJS_E_NOTIMPL; }
    tjs_error GetAsNumber(const tjs_char *, tjs_int64 *) override { return TJS_E_NOTIMPL; }
    tjs_error GetAsString(const tjs_char *, const tjs_char **) override { return TJS_E_NOTIMPL; }
    tjs_error GetValue(const tjs_char *name, tTJSVariant *out) override {
        const auto it=values.find(ttstr(name));
        if(it==values.end()) { out->Clear(); return TJS_S_OK; }
        *out=it->second; return TJS_S_OK;
    }
    void Int(const char *name, int value) { values[ttstr(name)]=tTJSVariant(value); }
    void Real(const char *name, double value) { values[ttstr(name)]=tTJSVariant(value); }
};
class ScanLines : public iTVPScanLineProvider {
public:
    int width,height,reads=0,writes=0;
    std::vector<uint32_t> pixels;
    iTVPTexture2D *texture=nullptr;
    ScanLines(int w,int h,bool gpu,int seed) : width(w),height(h),pixels(size_t(w)*h) {
        uint32_t random=0x78421013u+seed;
        constexpr uint32_t alphas[]={0,1,63,127,128,191,254,255};
        for(size_t i=0;i<pixels.size();++i) {
            random=random*1664525u+1013904223u;
            pixels[i]=(random&0xffffffu)|(alphas[(i+seed)&7]<<24);
        }
        if(gpu) {
            texture=TVPGetRenderManager()->CreateTexture2D(nullptr,0,w,h,TVPTextureFormat::RGBA);
            texture->Update(pixels.data(),TVPTextureFormat::RGBA,w*4,tTVPRect(0,0,w,h));
        }
    }
    ~ScanLines() { if(texture) texture->Release(); }
    tjs_error AddRef() override { return TJS_S_OK; }
    tjs_error Release() override { return TJS_S_OK; }
    tjs_error GetWidth(int *out) override { *out=width; return TJS_S_OK; }
    tjs_error GetHeight(int *out) override { *out=height; return TJS_S_OK; }
    tjs_error GetPixelFormat(int *out) override { *out=32; return TJS_S_OK; }
    tjs_error GetPitchBytes(int *out) override { *out=width*4; return TJS_S_OK; }
    tjs_error GetScanLine(int line,const void **out) override {
        ++reads; Check(line>=0 && line<height,"handler read row out of bounds");
        *out=texture ? texture->GetScanLineForRead(line) : pixels.data()+size_t(line)*width;
        return TJS_S_OK;
    }
    tjs_error GetScanLineForWrite(int line,void **out) override {
        ++writes; Check(line>=0 && line<height,"handler wrote row out of bounds");
        *out=texture ? texture->GetScanLineForWrite(line) : pixels.data()+size_t(line)*width;
        return TJS_S_OK;
    }
    iTVPTexture2D *GetTexture() override { return texture; }
    iTVPTexture2D *GetTextureForRender() override { return texture; }
};
class COWScanLines : public ScanLines {
    bool detached=false;
public:
    using ScanLines::ScanLines;
    iTVPTexture2D *GetTextureForRender() override {
        if(texture && !detached) {
            auto *replacement=TVPGetRenderManager()->CreateTexture2D(width,height,texture);
            texture->Release(); texture=replacement; detached=true;
        }
        return texture;
    }
};
// Two providers can expose the same mutable bitmap; COW through one provider
// then changes the current texture observed through the other provider.
class ScanLineAlias : public iTVPScanLineProvider {
    ScanLines &owner;
public:
    explicit ScanLineAlias(ScanLines &value) : owner(value) {}
    tjs_error AddRef() override { return TJS_S_OK; }
    tjs_error Release() override { return TJS_S_OK; }
    tjs_error GetWidth(int *out) override { return owner.GetWidth(out); }
    tjs_error GetHeight(int *out) override { return owner.GetHeight(out); }
    tjs_error GetPixelFormat(int *out) override { return owner.GetPixelFormat(out); }
    tjs_error GetPitchBytes(int *out) override { return owner.GetPitchBytes(out); }
    tjs_error GetScanLine(int row,const void **out) override { return owner.GetScanLine(row,out); }
    tjs_error GetScanLineForWrite(int row,void **out) override { return owner.GetScanLineForWrite(row,out); }
    iTVPTexture2D *GetTexture() override { return owner.GetTexture(); }
    iTVPTexture2D *GetTextureForRender() override { return owner.GetTextureForRender(); }
};
struct Registration {
    Registration() {
        RegisterMosaicTransHandlerProvider(); RegisterWaveTransHandlerProvider();
        RegisterRippleTransHandlerProvider(); RegisterTurnTransHandlerProvider();
        RegisterRotateTransHandlerProvider();
    }
    ~Registration() {
        UnregisterRotateTransHandlerProvider(); UnregisterTurnTransHandlerProvider();
        UnregisterRippleTransHandlerProvider(); UnregisterWaveTransHandlerProvider();
        UnregisterMosaicTransHandlerProvider();
    }
};
void Compare(const ScanLines &cpu,ScanLines &gpu,const char *handler,int tick,int partition) {
    for(int y=0;y<cpu.height;++y) {
        auto *actual=static_cast<const uint32_t *>(gpu.texture->GetScanLineForRead(y));
        for(int x=0;x<cpu.width;++x) if(cpu.pixels[size_t(y)*cpu.width+x]!=actual[x]) {
            std::cerr<<"extrans mismatch handler="<<handler<<" tick="<<tick
                     <<" partition="<<partition<<" x="<<x<<" y="<<y
                     <<" expected="<<cpu.pixels[size_t(y)*cpu.width+x]<<" actual="<<actual[x]<<'\n';
            throw std::runtime_error("real extrans CPU / production shader parity");
        }
    }
}
}

// The parity binary deliberately omits TVPTrans.cpp. Only provider registration
// is substituted; provider construction, time stepping, Process and tables are
// compiled from unchanged production translation units.
void TVPAddTransHandlerProvider(iTVPTransHandlerProvider *provider) {
    const tjs_char *name=nullptr; provider->GetName(&name);
    providers[ttstr(name)]=provider;
}
void TVPRemoveTransHandlerProvider(iTVPTransHandlerProvider *provider) {
    for(auto it=providers.begin();it!=providers.end();++it)
        if(it->second==provider) { providers.erase(it); return; }
}

// Apple uses the C ripple implementation. Only CPU feature detection is
// substituted, so portable tests cannot accidentally select x86 SIMD math.
tjs_uint32 TVPGetCPUType() { return 0; }

void TransitionHandlerTests(krkrsdl3::iTVPRenderBackend *) {
    Check(TVPHasMetalLayerTransitionSupport(),"test backend lacks transition compute support");
    Registration registration;
    uint64_t cases=0;
    const char *names[]={"mosaic","wave","ripple","turn","rotatezoom","rotatevanish","rotateswap"};
    for(const auto *name:names) for(int shape=0;shape<2;++shape)
    for(int variant=0;variant<(std::strcmp(name,"wave")==0 || std::strcmp(name,"mosaic")==0 ? 3 : std::strcmp(name,"ripple")==0 ? 5 : 2);++variant) {
        const int w=shape ? 135 : 128,h=shape ? 99 : 96;
        // Logical handler canvas, actual sources, and output have independent
        // sizes. Padded sources also exercise legacy turn's final block reads.
        const int sourceW=w+64,sourceH=h+64,outputW=w+7,outputH=h+9;
        Options options;
        options.Int("time",1000); options.Int("maxsize",variant==2 ? 2 : variant ? 9 : 30);
        options.Int("maxh",17); options.Real("maxomega",0.217);
        options.Int("wavetype",variant); options.Int("maxdrift",variant==4 ? std::min(w,h)-1 : 3);
        options.Int("rwidth",16<<std::min(variant,3));
        // The legacy ripple half-map assumes its center is on the larger half
        // of an odd canvas. Avoid the old out-of-table floor-center case.
        options.Int("centerx",variant&1 ? w/3 : (w+1)/2);
        options.Int("centery",variant&1 ? h/3 : (h+1)/2);
        options.Int("bgcolor",0x6478ab12); options.Int("bgcolor1",0x09123f5a);
        options.Int("bgcolor2",0x784312ed);
        options.Real("roundness",variant&1 ? 1.37 : 1.0);
        options.Real("speed",6.0); options.Real("factor",variant ? 0.42 : 0.1);
        options.Real("twist",variant ? -0.65 : 0.35);
        options.Real("accel",variant ? -2.0 : 2.0);
        options.Real("twistaccel",variant ? 0.0 : 1.5);
        auto layerType=std::strcmp(name,"wave")==0 ?
            (variant==0 ? ltOpaque : variant==1 ? ltAlpha : ltAddAlpha) : ltOpaque;
        iTVPBaseTransHandler *base=nullptr;
        auto *provider=providers.at(ttstr(name));
        Check(TJS_SUCCEEDED(provider->StartTransition(&options,nullptr,layerType,w,h,w,h,nullptr,nullptr,&base)),"provider construction failed");
        auto *handler=static_cast<iTVPDivisibleTransHandler *>(base);
        ScanLines source1(sourceW,sourceH,false,3),source2(sourceW,sourceH,false,11);
        ScanLines gpuSource1(sourceW,sourceH,true,3),gpuSource2(sourceW,sourceH,true,11);
        // Include time endpoints, timeout and jumps; turn covers every phase.
        std::vector<int> ticks={0,1,157,500,999,1000,1300};
        if(std::strcmp(name,"turn")==0) {
            ticks.clear(); for(int tick=0;tick<=1000;tick+=10) ticks.push_back(tick);
            ticks.push_back(1000); ticks.push_back(1300);
        }
        for(int tick:ticks) {
            Check(handler->StartProcess(100+tick)==TJS_S_TRUE,"StartProcess lifecycle changed");
            for(int partition=0;partition<3;++partition) {
                ScanLines expected(outputW,outputH,false,19),actual(outputW,outputH,true,19);
                std::vector<tTVPRect> rects;
                if(partition==0) rects.emplace_back(0,0,w,h);
                else if(partition==1) {
                    for(int y=h;y>0;) { int top=std::max(0,y-7); rects.emplace_back(0,top,w,y); y=top; }
                } else {
                    // Tiny and odd ROIs with nonzero source/destination origins.
                    rects.emplace_back(5,7,w-3,h-2); rects.emplace_back(1,2,2,3);
                }
                for(const auto &r:rects) {
                    tTVPDivisibleData data{};
                    data.Left=r.left; data.Top=r.top; data.Width=r.right-r.left; data.Height=r.bottom-r.top;
                    data.DestLeft=r.left+3; data.DestTop=r.top+4;
                    data.Dest=&expected; data.Src1=&source1; data.Src2=&source2;
                    const bool sameSource=tick==500 && partition==2;
                    if(sameSource) data.Src2=&source1;
                    Check(TJS_SUCCEEDED(handler->Process(&data)),"CPU handler Process failed");
                    data.Dest=&actual; data.Src1=&gpuSource1; data.Src2=&gpuSource2;
                    if(sameSource) data.Src2=&gpuSource1;
                    Check(TJS_SUCCEEDED(handler->Process(&data)),"GPU handler Process failed");
                }
                if(actual.writes || gpuSource1.reads || gpuSource2.reads)
                    std::cerr<<"extrans residency failed handler="<<name<<" shape="<<shape
                             <<" variant="<<variant<<" tick="<<tick<<" partition="<<partition<<'\n';
                Check(actual.writes==0 && gpuSource1.reads==0 && gpuSource2.reads==0,
                      "resident handler opened a CPU scanline");
                Compare(expected,actual,name,tick,partition); ++cases;
            }
            Check(handler->EndProcess()==(tick>=1000 ? TJS_S_FALSE : TJS_S_TRUE),"EndProcess lifecycle changed");
        }
        iTVPScanLineProvider *final=nullptr;
        Check(TJS_SUCCEEDED(handler->MakeFinalImage(&final,&source1,&source2)) && final==&source2,
              "final transition image changed");
        if(std::strcmp(name,"wave")==0) {
            // Actual destination/source alias preserves the CPU's sequential
            // scanline order. It must reject the resident path before writes.
            handler->StartProcess(600);
            ScanLines expected(outputW,outputH,false,19),actual(outputW,outputH,true,19);
            tTVPDivisibleData data{};
            data.Left=5; data.Top=7; data.Width=w-9; data.Height=h-12;
            data.DestLeft=8; data.DestTop=11;
            data.Dest=&expected; data.Src1=&expected; data.Src2=&source2;
            Check(TJS_SUCCEEDED(handler->Process(&data)),"alias CPU Process failed");
            data.Dest=&actual; data.Src1=&actual; data.Src2=&gpuSource2;
            Check(TJS_SUCCEEDED(handler->Process(&data)),"alias fallback Process failed");
            Check(actual.writes>0,"actual texture alias did not fall back before encoding");
            Compare(expected,actual,name,500,3); ++cases;
            COWScanLines expectedCOW(outputW,outputH,false,19),actualCOW(outputW,outputH,true,19);
            ScanLineAlias expectedAlias(expectedCOW),actualAlias(actualCOW);
            data.Dest=&expectedCOW; data.Src1=&expectedAlias; data.Src2=&source2;
            Check(TJS_SUCCEEDED(handler->Process(&data)),"CPU provider alias Process failed");
            data.Dest=&actualCOW; data.Src1=&actualAlias; data.Src2=&gpuSource2;
            Check(TJS_SUCCEEDED(handler->Process(&data)),"COW provider alias fallback Process failed");
            Check(actualCOW.writes>0,"COW changed a logical source alias into a snapshot GPU operation");
            Compare(expectedCOW,actualCOW,name,500,4); ++cases;
        }
        handler->Release();
    }
    // Unsafe ripple maxdrift=0 previously constructed an empty drift table.
    Options invalid; invalid.Int("time",1000); invalid.Int("maxdrift",0);
    iTVPBaseTransHandler *handler=nullptr; bool rejected=false;
    try { providers.at(ttstr("ripple"))->StartTransition(&invalid,nullptr,ltOpaque,128,96,128,96,nullptr,nullptr,&handler); }
    catch(const std::exception &) { rejected=true; }
    Check(rejected && !handler,"ripple maxdrift zero must be rejected before table construction");
    Options oddCenter; oddCenter.Int("time",1000); oddCenter.Int("maxdrift",3);
    rejected=false;
    try { providers.at(ttstr("ripple"))->StartTransition(&oddCenter,nullptr,ltOpaque,135,99,135,99,nullptr,nullptr,&handler); }
    catch(const std::exception &) { rejected=true; }
    Check(rejected && !handler,"ripple undefined odd floor-center must be rejected before table construction");
    // With the original unsigned HalfTime division, maxsize=1 gives a negative
    // block at tick 1 (-1752346655 on the supported 32-bit-int platforms).
    const int originalBlock=static_cast<int>((int64_t(1-2)*int64_t(1))/uint64_t(500)+2);
    Check(originalBlock<0,"mosaic maxsize=1 unsafe-domain regression assumption changed");
    Options invalidMosaic; invalidMosaic.Int("time",1000); invalidMosaic.Int("maxsize",1);
    rejected=false;
    try { providers.at(ttstr("mosaic"))->StartTransition(&invalidMosaic,nullptr,ltOpaque,128,96,128,96,nullptr,nullptr,&handler); }
    catch(const std::exception &) { rejected=true; }
    Check(rejected && !handler,"mosaic undefined maxsize=1 must be rejected before processing");
    std::cout<<"PASS real seven extrans handlers: "<<cases<<" exact CPU / production shader frame-region cases\n";
}

void TransitionHandlerDispatchFailureTests(const std::function<void(bool)> &inject) {
    Registration registration;
    const char *names[]={"mosaic","wave","ripple","turn","rotatezoom","rotatevanish","rotateswap"};
    for(const auto *name:names) {
        Options options; options.Int("time",1000); options.Int("maxdrift",3);
        iTVPBaseTransHandler *base=nullptr;
        Check(TJS_SUCCEEDED(providers.at(ttstr(name))->StartTransition(&options,nullptr,ltOpaque,
            128,96,128,96,nullptr,nullptr,&base)),"fault fixture provider construction failed");
        auto *handler=static_cast<iTVPDivisibleTransHandler *>(base);
        handler->StartProcess(100); handler->StartProcess(600);
        ScanLines source1(192,160,true,3),source2(192,160,true,11),dest(135,105,true,19);
        tTVPDivisibleData data{};
        data.Left=5; data.Top=7; data.Width=80; data.Height=60;
        data.DestLeft=8; data.DestTop=11;
        data.Dest=&dest; data.Src1=&source1; data.Src2=&source2;
        inject(true);
        bool propagated=false;
        try { handler->Process(&data); }
        catch(const std::bad_alloc &) { propagated=true; }
        catch(...) { inject(false); handler->Release(); throw; }
        inject(false); handler->Release();
        if(!propagated) std::cerr<<"post-dispatch exception failed handler="<<name<<'\n';
        Check(propagated,"post-dispatch bad_alloc was swallowed by the handler");
        Check(dest.writes==0 && source1.reads==0 && source2.reads==0,
              "post-dispatch failure replayed the transition ROI on CPU");
    }
    std::cout<<"PASS all seven real extrans propagate post-dispatch bad_alloc without CPU replay\n";
}

void TransitionHandlerPipelineFailureTests(const std::function<void(bool)> &inject) {
    Check(TVPHasMetalLayerTransitionSupport(),"pipeline rejection must retain the support probe");
    Registration registration;
    const char *names[]={"mosaic","wave","ripple","turn","rotatezoom","rotatevanish","rotateswap"};
    for(const auto *name:names) {
        Options options; options.Int("time",1000); options.Int("maxdrift",3);
        iTVPBaseTransHandler *base=nullptr;
        Check(TJS_SUCCEEDED(providers.at(ttstr(name))->StartTransition(&options,nullptr,ltOpaque,
            128,96,128,96,nullptr,nullptr,&base)),"pipeline fixture provider construction failed");
        auto *handler=static_cast<iTVPDivisibleTransHandler *>(base);
        handler->StartProcess(100); handler->StartProcess(600);
        ScanLines source1(192,160,false,3),source2(192,160,false,11),expected(135,105,false,19);
        ScanLines gpuSource1(192,160,true,3),gpuSource2(192,160,true,11),actual(135,105,true,19);
        const auto unchanged=expected.pixels;
        tTVPDivisibleData data{};
        data.Left=5; data.Top=7; data.Width=80; data.Height=60;
        data.DestLeft=8; data.DestTop=11;
        data.Dest=&expected; data.Src1=&source1; data.Src2=&source2;
        Check(TJS_SUCCEEDED(handler->Process(&data)),"pipeline fixture CPU oracle failed");
        data.Dest=&actual; data.Src1=&gpuSource1; data.Src2=&gpuSource2;
        inject(true);
        try {
            Check(TVPHasMetalLayerTransitionSupport(),"reject flag incorrectly disabled the capability probe");
            Check(TJS_SUCCEEDED(handler->Process(&data)),"pipeline rejection did not complete through CPU fallback");
        }
        catch(...) { inject(false); handler->Release(); throw; }
        inject(false); handler->Release();
        Check(actual.writes>0 && gpuSource1.reads>0 && gpuSource2.reads>0,
              "pipeline rejection bypassed the real CPU handler scanlines");
        Compare(expected,actual,name,500,5);
        for(int y=0;y<expected.height;++y) for(int x=0;x<expected.width;++x)
            if(x<8 || x>=88 || y<11 || y>=71)
                Check(expected.pixels[size_t(y)*expected.width+x]==unchanged[size_t(y)*expected.width+x],
                      "pipeline fallback changed pixels outside the destination ROI");
    }
    std::cout<<"PASS all seven real extrans pipeline rejection: exact CPU fallback and unchanged ROI exterior\n";
}
