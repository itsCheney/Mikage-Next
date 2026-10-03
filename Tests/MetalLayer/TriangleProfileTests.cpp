#include "tjsCommHead.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "LayerTriangleTrace.h"
#include "../../Engine/KRKRRuntime/Source/host/MikageKRKRRuntime.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// Compile the exact production bridge. Only host lifecycle/log registration
// globals are substituted; every field copy and interval reset is production.
static bool running=true;
static void TestLog(const char*,int32_t,const char*) {}
static std::atomic<MikageKRKRLogCallback> diagnosticCallback{TestLog};
#include "ProductionTriangleProfileBridge.inc"

extern bool TVPTestCaptureLogs;
extern std::vector<std::string> TVPTestLogs;
namespace {
namespace trace=krkrsdl3::layer_triangle_trace;
using Texture=std::unique_ptr<iTVPTexture2D>;
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
Texture Create(int w=8,int h=6) {
    return Texture(TVPGetRenderManager()->CreateTexture2D(nullptr,0,w,h,TVPTextureFormat::RGBA));
}
void Fill(iTVPTexture2D* t,uint32_t color) {
    auto* mgr=TVPGetRenderManager();
    auto* fill=mgr->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,color);
    mgr->SetParameterInt(mgr->EnumParameterID("StretchType"),0);
    mgr->OperateRect(fill,t,nullptr,tTVPRect(0,0,t->GetWidth(),t->GetHeight()),tRenderTexRectArray(nullptr,0));
}
void Triangle(iTVPTexture2D* t,iTVPTexture2D* s,const tTVPRect& clip,
              const char* name="Copy",iTVPTexture2D* reference=nullptr) {
    const tTVPPointD points[]={{1,1},{5,1},{1,4},{5,1},{1,4},{5,4}};
    auto* mgr=TVPGetRenderManager(); auto* method=mgr->GetRenderMethod(name);
    method->SetParameterOpa(0,127);
    std::pair<iTVPTexture2D*,const tTVPPointD*> input(s,points);
    mgr->OperateTriangles(method,2,t,reference,clip,points,tRenderTexQuadArray(&input,1));
}
MikageKRKRLayerTriangleProfile Take() {
    MikageKRKRLayerTriangleProfile p;
    std::memset(&p,0xa5,sizeof(p));
    Require(MikageKRKRTakeLayerTriangleProfile(&p),"production triangle bridge did not sample");
    Require(std::memchr(p.methods,0,sizeof(p.methods)) && std::memchr(p.targetSizes,0,sizeof(p.targetSizes)) &&
            std::memchr(p.sources,0,sizeof(p.sources)) && std::memchr(p.stretchModes,0,sizeof(p.stretchModes)),
            "triangle bridge string was not NUL terminated");
    return p;
}
uint64_t HistogramCalls(const char* histogram) {
    std::string s(histogram); uint64_t count=0;
    size_t start=0;
    while(start<s.size()) {
        const auto end=s.find(',',start),colon=s.find(':',start);
        Require(colon!=std::string::npos,"malformed triangle histogram");
        count+=std::stoull(s.substr(colon+1,end-colon-1));
        if(end==std::string::npos) break;
        start=end+1;
    }
    return count;
}
}

void TriangleProfileTests() {
    static_assert(!std::is_copy_constructible<trace::SourceScope>::value);
    auto t=Create(),s=Create(),r=Create();
    auto* mgr=TVPGetRenderManager();
    Require(mgr->GetRenderMethod("AlphaBlend")==mgr->GetRenderMethod("AlphaBlend_HDA") &&
            mgr->GetRenderMethod("AlphaBlend")->GetName()=="AlphaBlend" &&
            mgr->GetRenderMethod("AlphaBlend_a")==mgr->GetRenderMethod("PerspectiveAlphaBlend_a") &&
            mgr->GetRenderMethod("AlphaBlend_a")->GetName()=="AlphaBlend_a",
            "aliases changed canonical method attribution or object identity");
    constexpr uint64_t bytes=8*6*4;
    TVPSetMetalLayerTriangleDiagnostics(false);
    Fill(t.get(),0xff102030); Fill(s.get(),0xff8090a0);
    const auto disabledBefore=TVPGetMetalLayerRenderStats();
    Triangle(t.get(),s.get(),tTVPRect(0,0,8,6));
    Require(TVPGetMetalLayerRenderStats().cpuFallbacks==disabledBefore.cpuFallbacks+1,
            "disabling diagnostics changed triangle routing");
    Require(TVPTakeMetalLayerTriangleProfile().stats.calls==0,"disabled diagnostics collected triangles");
    { trace::SourceScope disabled(trace::Source::AffineCopy);
      Require(trace::source==trace::Source::Unknown,"disabled source scope acquired a tag"); }

    TVPSetMetalLayerTriangleDiagnostics(true);
    Fill(t.get(),0xff102030); Fill(s.get(),0xff8090a0); Fill(r.get(),0xff334455);
    TVPTestLogs.clear(); TVPTestCaptureLogs=true;
    const auto before=TVPGetMetalLayerRenderStats();
    {
        trace::SourceScope outer(trace::Source::AffineCopy);
        Triangle(t.get(),s.get(),tTVPRect(-3,-2,99,99),"Copy",r.get());
        try {
            trace::SourceScope inner(trace::Source::AffineBlend);
            auto* mgr=TVPGetRenderManager(); mgr->SetParameterInt(mgr->EnumParameterID("StretchType"),1);
            Triangle(t.get(),s.get(),tTVPRect(2,2,4,3),"AlphaBlend");
            throw std::runtime_error("unwind");
        } catch(const std::runtime_error&) {}
        Require(trace::source==trace::Source::AffineCopy,"nested source scope failed to restore on unwind");
    }
    Require(trace::source==trace::Source::Unknown,"source tag leaked after affine operation");
    TVPGetRenderManager()->SetParameterInt(TVPGetRenderManager()->EnumParameterID("StretchType"),0);
    Triangle(t.get(),s.get(),tTVPRect(9,7,10,8));
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.cpuFallbacks-before.cpuFallbacks==3 && after.readbackBytes-before.readbackBytes==bytes*3,
            "triangle diagnostics changed fallback counts/readbacks");
    Require(TVPTestLogs.empty(),"triangle diagnostics wrote per-operation logs");
    TVPTestCaptureLogs=false;
    // Read-only/HUD stats must not consume the diagnostic interval.
    TVPGetMetalLayerRenderStats(); TVPGetMetalLayerRenderStats();
    Require(!MikageKRKRTakeLayerTriangleProfile(nullptr),"bridge accepted null output");
    running=false; MikageKRKRLayerTriangleProfile ignored{};
    Require(!MikageKRKRTakeLayerTriangleProfile(&ignored),"bridge sampled stopped host");
    running=true; diagnosticCallback.store(nullptr);
    Require(!MikageKRKRTakeLayerTriangleProfile(&ignored),"bridge sampled without diagnostic callback");
    diagnosticCallback.store(TestLog);
    const auto p=Take();
    Require(p.intervalNS>0 && p.calls==3 && p.triangleCount==6,"triangle interval counts lost/reset by other readers");
    Require(p.clipPixels==50 && p.maxClipPixels==48 && p.maxTargetPixels==48 && p.fullSurfaceCalls==1 &&
            p.target1920x1080Calls==0,"triangle clip/target area accounting incorrect");
    Require(p.targetReadbackBytes==bytes && p.sourceReadbackBytes==bytes && p.referenceReadbackBytes==bytes,
            "triangle actual readback roles/cache hits incorrect");
    Require(p.cpuTimeNS>0 && p.maxCpuTimeNS>0 && p.cpuTimeNS>=p.softwareTimeNS &&
            p.cpuTimeNS>=p.maxCpuTimeNS && p.maxCpuTimeNS>=p.maxSoftwareTimeNS &&
            p.softwareTimeNS>=p.maxSoftwareTimeNS,"triangle timing totals/maxima incorrect");
    Require(p.count2Calls==3 && p.singleInputCalls==3 && p.referenceCalls==1 && p.sourceTargetAliasCalls==0,
            "triangle call-shape accounting incorrect");
    Require(std::string(p.methods)=="Copy:2,AlphaBlend:1","triangle method histogram incorrect");
    Require(std::string(p.targetSizes)=="8x6:3","triangle target histogram incorrect");
    Require(std::string(p.sources)=="unknown:1,AffineCopy:1,AffineBlend:1","triangle source histogram incorrect");
    Require(std::string(p.stretchModes)=="0:2,1:1","triangle sampling histogram incorrect");
    auto empty=Take();
    Require(empty.calls==0 && empty.clipPixels==0 && empty.maxClipPixels==0 && empty.cpuTimeNS==0 &&
            empty.maxCpuTimeNS==0 && empty.maxTargetPixels==0 && empty.methods[0]==0 && empty.sources[0]==0,
            "triangle interval counters/maxima/histograms did not reset");
    const auto* pixels=static_cast<const uint32_t*>(t->GetScanLineForRead(0));
    Require(pixels[0]==0xff102030 && pixels[1+8]==0xff8090a0,
            "diagnostic instrumentation changed affine fallback pixels");

    // Aliases are charged to the first CPUViews role, never counted twice.
    Fill(t.get(),0xff123456);
    { trace::SourceScope tag(trace::Source::AffinePile);
      Triangle(t.get(),t.get(),tTVPRect(0,0,8,6),"Copy",t.get()); }
    Fill(r.get(),0xff654321);
    { trace::SourceScope tag(trace::Source::OperateAffine);
      Triangle(t.get(),r.get(),tTVPRect(0,0,8,6),"Copy",r.get()); }
    auto aliases=Take();
    Require(aliases.calls==2 && aliases.referenceCalls==2 && aliases.sourceTargetAliasCalls==1 &&
            aliases.targetReadbackBytes==bytes && aliases.referenceReadbackBytes==bytes && aliases.sourceReadbackBytes==0,
            "aliased target/reference/source readbacks were double counted");
    Require(std::string(aliases.sources)=="AffinePile:1,OperateAffine:1","affine source attribution missing");

    // Full HD clip versus a tiny raster region proves this measures clip area.
    auto hd=Create(1920,1080); Fill(hd.get(),0xff112233);
    Triangle(hd.get(),s.get(),tTVPRect(0,0,1920,1080));
    auto fullHD=Take();
    Require(fullHD.target1920x1080Calls==1 && fullHD.maxTargetPixels==1920*1080 &&
            fullHD.clipPixels==1920*1080 && fullHD.fullSurfaceCalls==1 &&
            fullHD.targetReadbackBytes==1920*1080*4 && std::string(fullHD.targetSizes)=="1920x1080:1",
            "full-HD triangle readback/target diagnostics incorrect");

    // A burst of distinct target sizes cannot grow the interval unboundedly.
    for(int w=8;w<48;++w) {
        auto varied=Create(w,6); Fill(varied.get(),0xff112233);
        Triangle(varied.get(),s.get(),tTVPRect(0,0,w,6));
    }
    auto bounded=Take();
    Require(bounded.calls==40 && HistogramCalls(bounded.targetSizes)==40 &&
            std::string(bounded.targetSizes).find("otherCalls:32")!=std::string::npos,
            "bounded target histogram lost omitted calls");

    Triangle(t.get(),s.get(),tTVPRect(0,0,8,6));
    TVPSetMetalLayerTriangleDiagnostics(false);
    TVPSetMetalLayerTriangleDiagnostics(true);
    Require(Take().calls==0,"diagnostic disable/re-enable retained an old interval");
    TVPSetMetalLayerTriangleDiagnostics(false);
    std::cout<<"PASS triangle attribution: interval/bridge, areas, actual readbacks/cache/aliases, disabled, bounded histograms\n";
}
