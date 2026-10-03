#include "tjsCommHead.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "TVPCompositor.h"
#include "PointReadTrace.h"
#include "gl/tvpgl.h"
#ifdef TEST_NATIVE_METAL
#include "backend/MetalRenderBackend.h"
#include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
using krkrsdl3::iTVPRenderBackend;
using Texture=std::unique_ptr<iTVPTexture2D>;
extern bool TVPTestCaptureLogs;
extern std::vector<std::string> TVPTestLogs;
void BitmapOverwriteTests(iTVPRenderBackend* backend);
void UnivTransShaderTests();
void TriangleProfileTests();
uint32_t TVPTestUnivTransPixel(uint32_t,uint32_t,uint8_t,const TVPLayerOperation&);

#ifdef TEST_NATIVE_METAL
// This standalone parity binary links the Metal backend directly rather than
// the engine compositor. Count submissions/waits/blits for transition assertions;
// the remaining runtime profiling hooks are no-ops.
static uint64_t testMetalSubmits=0,testMetalWaits=0,testMetalBlits=0;
namespace krkrsdl3 {
void TVPRecordMeshDraw(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {}
void TVPRecordEmoteGPUDeform(uint64_t) {}
void TVPRecordMetalSubmit() { ++testMetalSubmits; }
void TVPRecordMetalRenderEncoder() {}
void TVPRecordMetalComputeEncoder() {}
void TVPRecordMetalBlitEncoder() { ++testMetalBlits; }
void TVPRecordMetalLayerRectSnapshot(uint64_t) {}
void TVPRecordMetalSurfaceUpload(uint64_t) {}
void TVPRecordMetalSyncWait(uint64_t) { ++testMetalWaits; }
void TVPRecordMetalQueueWait(uint64_t) {}
void TVPRecordMetalRingSuballoc(uint64_t, uint64_t, uint64_t) {}
void TVPRecordMetalRingWrap() {}
void TVPRecordMetalRingStall(uint64_t) {}
void TVPRecordMetalRingFallback(uint64_t) {}
}
#endif
static void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
static tTVPRect Rect(const TVPLayerRect& r) { return tTVPRect(r.left,r.top,r.right,r.bottom); }
#ifndef TEST_NATIVE_METAL
static std::vector<krkrsdl3::point_trace::Query> pointReadObservations;
// A synchronous device double exercises production GPU texture/cache/session
// logic on non-Apple hosts. It dispatches to actual software methods rather
// than reproducing Metal math. Pixel equivalence is tested on native Metal CI.
class DeviceDouble : public iTVPRenderBackend {
    struct Resource { std::vector<uint8_t> pixels; int w,h,bpp; };
    std::unordered_map<void*,std::unique_ptr<Resource>> resources;
public:
    bool rejectTripleSource=false;
    const char* GetName() const override { return "test-device"; }
    void BeginFrame(int,int) override {} void EndFrame() override {}
    void* CreateWindowTexture(int,int) override { return nullptr; }
    void UpdateWindowTexture(void*,const uint8_t*,int,int,int) override {}
    void DestroyWindowTexture(void*) override {}
    void DrawWindowTexture(void*,float,float,float,float) override {}
    void* CreateTarget(int,int) override { return nullptr; } void DestroyTarget(void*) override {}
    void SetTarget(void*) override {} void ClearTarget(bool) override {}
    uint8_t* LockTarget(void*,int&) override { return nullptr; } void UnlockTarget(void*) override {}
    void* GetTargetTexture(void*) override { return nullptr; }
    void UpdateTargetTexture(void*,const uint8_t*,int,int,int) override {}
    void* CreateTexture(int,int) override { return nullptr; }
    void UpdateTexture(void*,const uint8_t*,int,int,int) override {} void DestroyTexture(void*) override {}
    void SetMask(void*) override {} void SetBlendMode(int,const float*) override {}
    void DrawMesh(const float*,int,const uint16_t*,int,void*,float,const float*) override {}
    void LayerSetBlend(int,float,const float*) override {}
    void LayerDrawRect(void*,float,float,float,float,float,float,float,float) override {}
    bool SupportsLayerOperations() const override { return true; }
    bool SetLayerAlphaTables(const uint8_t*,const uint8_t*) override { return true; }
    void* CreateLayerTexture(int w,int h,TVPLayerTextureFormat f) override {
        auto r=std::make_unique<Resource>(); r->w=w;r->h=h;r->bpp=f==TVPLayerTextureFormat::R8?1:4;
        r->pixels.resize(size_t(w)*h*r->bpp); auto* key=r.get(); resources[key]=std::move(r); return key;
    }
    void DestroyLayerTexture(void* handle) override { resources.erase(handle); }
    bool UpdateLayerTexture(void* handle,const uint8_t* data,int pitch,const TVPLayerRect& rc) override {
        auto& r=*resources.at(handle);
        for(int y=0;y<rc.Height();++y) std::memcpy(r.pixels.data()+((y+rc.top)*r.w+rc.left)*r.bpp,data+y*pitch,rc.Width()*r.bpp);
        return true;
    }
    bool ReadLayerTexture(void* handle,std::vector<uint8_t>& pixels,int& pitch) override {
        auto& r=*resources.at(handle);pixels=r.pixels;pitch=r.w*r.bpp;return true;
    }
    bool CopyTargetToLayerTexture(void* source,void* destination) override {
        auto s=resources.find(source), d=resources.find(destination);
        if(s==resources.end() || d==resources.end() || source==destination) return false;
        const auto& src=*s->second; auto& dst=*d->second;
        if(src.bpp!=4 || dst.bpp!=4 || src.w!=dst.w || src.h!=dst.h) return false;
        dst.pixels=src.pixels;
        return true;
    }
    bool ReadLayerTextureRegion(void* handle,const TVPLayerRect& rc,std::vector<uint8_t>& pixels,int& pitch) override {
        auto& r=*resources.at(handle);pitch=rc.Width()*r.bpp;pixels.resize(size_t(pitch)*rc.Height());
        for(int y=0;y<rc.Height();++y) std::memcpy(pixels.data()+y*pitch,r.pixels.data()+((y+rc.top)*r.w+rc.left)*r.bpp,pitch);
        if(auto* query=krkrsdl3::point_trace::CurrentQuery()) {
            // Simulated backend handoff only; no sleep or claimed GPU timing.
            query->reported=true; query->lastSubmittedID=73; query->renderFrame=19;
            query->wallNS=12000000; query->gpuWaitNS=11000000; query->finishedNS=123000000;
            pointReadObservations.push_back(*query);
        }
        return true;
    }
    bool OperateLayerRect(const TVPLayerOperation& op,void* target,const TVPLayerRect& dst,void* source,const TVPLayerRect& src,int sampling) override {
        auto* sw=TVPGetSoftwareRenderManager(); const char* name=nullptr;
        switch(op.kind) {
            case TVPLayerOperationKind::Copy: name="Copy"; break;
            case TVPLayerOperationKind::CopyColor: name="CopyColor"; break;
            case TVPLayerOperationKind::CopyMask: name="CopyMask"; break;
            case TVPLayerOperationKind::CopyOpaque: name="CopyOpaqueImage"; break;
            case TVPLayerOperationKind::Fill: name="FillARGB"; break;
            case TVPLayerOperationKind::FillColor: name="FillColor"; break;
            case TVPLayerOperationKind::FillMask: name="FillMask"; break;
            case TVPLayerOperationKind::FillBlend: name="ConstColorAlphaBlend"; break;
            case TVPLayerOperationKind::RemoveConstOpacity: name="RemoveConstOpacity"; break;
            default:
                name=op.kind==TVPLayerOperationKind::Alpha ? "AlphaBlend" : op.kind==TVPLayerOperationKind::ConstAlpha ? "ConstAlphaBlend" : "ApplyColorMap";
                break;
        }
        std::string methodName=name;
        if(op.kind>=TVPLayerOperationKind::Alpha) {
            if(op.flags&TVP_LAYER_DEST_ALPHA) methodName+="_d";
            else if(op.flags&TVP_LAYER_DEST_PREMULTIPLIED) methodName+="_a";
            else if(op.kind==TVPLayerOperationKind::ConstAlpha && (op.flags&TVP_LAYER_HOLD_ALPHA)) methodName+="_HDA";
        }
        auto* method=sw->GetRenderMethod(methodName.c_str());
        method->SetParameterOpa(method->EnumParameterID("opacity"),op.opacity);
        method->SetParameterColor4B(method->EnumParameterID("color"),op.color);
        auto& t=*resources.at(target);
        Texture tv(sw->CreateTexture2D(t.pixels.data(),t.w*t.bpp,t.w,t.h,t.bpp==1?TVPTextureFormat::Gray:TVPTextureFormat::RGBA));
        Texture sv; std::vector<uint8_t> snapshot;
        std::pair<iTVPTexture2D*,tTVPRect> input;
        if(source) {
            auto& s=*resources.at(source);
            const void* pixels=s.pixels.data();
            if(source==target) {snapshot=s.pixels;pixels=snapshot.data();}
            sv.reset(sw->CreateTexture2D(pixels,s.w*s.bpp,s.w,s.h,s.bpp==1?TVPTextureFormat::Gray:TVPTextureFormat::RGBA));
            input={sv.get(),Rect(src)};
        }
        sw->SetParameterInt(sw->EnumParameterID("StretchType"),sampling);
        sw->OperateRect(method,tv.get(),nullptr,Rect(dst),tRenderTexRectArray(source?&input:nullptr,source?1:0));return true;
    }
    bool OperateLayerRectDualSource(const TVPLayerOperation& op,void* target,const TVPLayerRect& dst,
                                    void* source1,const TVPLayerRect& src1,
                                    void* source2,const TVPLayerRect& src2) override {
        if(op.kind!=TVPLayerOperationKind::ConstAlphaSD || !target || !source1 || !source2) return false;
        auto* sw=TVPGetSoftwareRenderManager();
        const char* name=(op.flags&TVP_LAYER_DEST_ALPHA) ? "ConstAlphaBlend_SD_d" : "ConstAlphaBlend_SD";
        auto* method=sw->GetRenderMethod(name);
        method->SetParameterOpa(method->EnumParameterID("opacity"),op.opacity);
        auto& t=*resources.at(target);
        auto& a=*resources.at(source1);
        auto& b=*resources.at(source2);
        std::vector<uint8_t> snapA,snapB;
        const void* pa=a.pixels.data(); const void* pb=b.pixels.data();
        if(source1==target) { snapA=a.pixels; pa=snapA.data(); }
        if(source2==target) { snapB=b.pixels; pb=snapB.data(); }
        Texture tv(sw->CreateTexture2D(t.pixels.data(),t.w*t.bpp,t.w,t.h,TVPTextureFormat::RGBA));
        Texture av(sw->CreateTexture2D(pa,a.w*a.bpp,a.w,a.h,TVPTextureFormat::RGBA));
        Texture bv(sw->CreateTexture2D(pb,b.w*b.bpp,b.w,b.h,TVPTextureFormat::RGBA));
        std::pair<iTVPTexture2D*,tTVPRect> inputs[]={{av.get(),Rect(src1)},{bv.get(),Rect(src2)}};
        sw->OperateRect(method,tv.get(),nullptr,Rect(dst),tRenderTexRectArray(inputs));
        return true;
    }
    bool OperateLayerRectTripleSource(const TVPLayerOperation& op,void* target,const TVPLayerRect& dst,
                                      void* source1,const TVPLayerRect& src1,
                                      void* source2,const TVPLayerRect& src2,
                                      void* rule,const TVPLayerRect& ruleRect) override {
        if(rejectTripleSource || op.kind!=TVPLayerOperationKind::UnivTrans ||
           !target || !source1 || !source2 || !rule) return false;
        auto& t=*resources.at(target); auto& a=*resources.at(source1);
        auto& b=*resources.at(source2); auto& r=*resources.at(rule);
        if(t.bpp!=4 || a.bpp!=4 || b.bpp!=4 || r.bpp!=1) return false;
        // Take independent snapshots before writing, including offset overlaps.
        auto first=a.pixels,second=b.pixels;
        for(int y=std::max(0,dst.top);y<std::min(t.h,dst.bottom);++y)
        for(int x=std::max(0,dst.left);x<std::min(t.w,dst.right);++x) {
            int dx=x-dst.left,dy=y-dst.top;
            uint32_t s1,s2;
            std::memcpy(&s1,first.data()+((src1.top+dy)*a.w+src1.left+dx)*4,4);
            std::memcpy(&s2,second.data()+((src2.top+dy)*b.w+src2.left+dx)*4,4);
            uint8_t weight=r.pixels[(ruleRect.top+dy)*r.w+ruleRect.left+dx];
            uint32_t out=TVPTestUnivTransPixel(s1,s2,weight,op);
            std::memcpy(t.pixels.data()+(y*t.w+x)*4,&out,4);
        }
        return true;
    }
};
#endif
static std::vector<uint8_t> Image(int w,int h,int bpp,int seed) {
    std::vector<uint8_t> pixels(size_t(w)*h*bpp);
    const uint8_t alpha[]={0,1,63,127,128,191,254,255};
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) for(int c=0;c<bpp;++c)
        pixels[(y*w+x)*bpp+c]=(c==3 || bpp==1)?alpha[(x+y+seed)&7]:uint8_t((x*17+y*11+c*53+seed*19)%220+16);
    return pixels;
}
static Texture Create(iTVPRenderManager* manager,int w,int h,TVPTextureFormat::e f,const std::vector<uint8_t>& pixels) {
    Texture t(manager->CreateTexture2D(nullptr,0,w,h,f));
    t->Update(pixels.data(),f,w*(f==TVPTextureFormat::Gray?1:4),tTVPRect(0,0,w,h)); return t;
}
static int comparisons=0;
static void Compare(iTVPTexture2D* expected,iTVPTexture2D* actual,int tolerance,const char* label) {
    for(unsigned y=0;y<expected->GetHeight();++y) {
        auto* e=static_cast<const uint8_t*>(expected->GetScanLineForRead(y));
        auto* a=static_cast<const uint8_t*>(actual->GetScanLineForRead(y));
        for(unsigned x=0;x<expected->GetWidth()*4;++x) if(std::abs(int(e[x])-int(a[x]))>tolerance) {
            std::cerr << label << " pixel byte " << x << ',' << y << " expected=" << int(e[x]) << " actual=" << int(a[x]) << '\n';
            throw std::runtime_error("software/Metal pixel mismatch");
        }
    }
    ++comparisons;
}
static void Operation(iTVPRenderManager* manager,iTVPRenderMethod* method,iTVPTexture2D* dst,const tTVPRect& dr,iTVPTexture2D* src,const tTVPRect& sr) {
    std::pair<iTVPTexture2D*,tTVPRect> input(src,sr);
    manager->OperateRect(method,dst,nullptr,dr,tRenderTexRectArray(src?&input:nullptr,src?1:0));
}
static void Equivalence() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    const char* methods[]={"Copy","CopyColor","CopyMask","CopyOpaqueImage","FillARGB","FillColor","FillMask",
        "AlphaBlend","AlphaBlend_HDA","AlphaBlend_d","AlphaBlend_a","ConstAlphaBlend","ConstAlphaBlend_HDA","ConstAlphaBlend_d","ConstAlphaBlend_a",
        "ApplyColorMap","ApplyColorMap_d","ApplyColorMap_a","ConstColorAlphaBlend","ConstColorAlphaBlend_d","ConstColorAlphaBlend_a",
        "RemoveConstOpacity"};
    for(auto* name:methods) for(int opacity:{0,1,63,127,254,255}) {
        auto* method=sw->GetRenderMethod(name); Require(method==gpu->GetRenderMethod(name),"canonical method pointer changed");
        method->SetParameterOpa(method->EnumParameterID("opacity"),opacity);
        method->SetParameterColor4B(method->EnumParameterID("color"),0x9139a7e2);
        TVPLayerOperation op; Require(method->DescribeGpuOperation(op),"missing semantic descriptor");
        bool glyph=op.kind==TVPLayerOperationKind::ColorMap;
        bool fill=op.kind==TVPLayerOperationKind::Fill || op.kind==TVPLayerOperationKind::FillColor ||
            op.kind==TVPLayerOperationKind::FillMask || op.kind==TVPLayerOperationKind::FillBlend ||
            op.kind==TVPLayerOperationKind::RemoveConstOpacity;
        auto source=Image(17,13,glyph?1:4,2), destination=Image(17,13,4,5);
        auto f=glyph?TVPTextureFormat::Gray:TVPTextureFormat::RGBA;
        auto ss=Create(sw,17,13,f,source), gs=Create(gpu,17,13,f,source);
        auto sd=Create(sw,17,13,TVPTextureFormat::RGBA,destination), gd=Create(gpu,17,13,TVPTextureFormat::RGBA,destination);
        tTVPRect dr(3,2,14,11),sr(1,1,12,10);
        auto before=TVPGetMetalLayerRenderStats();
        Operation(sw,method,sd.get(),dr,fill?nullptr:ss.get(),sr);
        Operation(gpu,method,gd.get(),dr,fill?nullptr:gs.get(),sr);
        auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks,"supported operator fell back");
        Require(after.readbackBytes==before.readbackBytes,"operator performed a CPU readback");
        Compare(sd.get(),gd.get(),op.kind<TVPLayerOperationKind::Alpha?0:1,name);
    }
    for(int sampling:{0,1,2}) for(int width:{1,2,7}) for(int height:{1,3,5}) {
        auto image=Image(width,height,4,2),destination=Image(11,9,4,4);
        auto ss=Create(sw,width,height,TVPTextureFormat::RGBA,image),gs=Create(gpu,width,height,TVPTextureFormat::RGBA,image);
        for(auto dr:{tTVPRect(2,1,10,8),tTVPRect(-2,-1,13,11),tTVPRect(2,2,4,4)}) {
            auto sd=Create(sw,11,9,TVPTextureFormat::RGBA,destination),gd=Create(gpu,11,9,TVPTextureFormat::RGBA,destination);
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
            auto* method=gpu->GetRenderMethod("Copy");
            Operation(sw,method,sd.get(),dr,ss.get(),tTVPRect(0,0,width,height));
            auto before=TVPGetMetalLayerRenderStats();
            Operation(gpu,method,gd.get(),dr,gs.get(),tTVPRect(0,0,width,height));
            Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks,"resize fell back");
            std::string label="resize sampling="+std::to_string(sampling)+" source="+std::to_string(width)+"x"+std::to_string(height)+" destination="+std::to_string(dr.left)+","+std::to_string(dr.top)+","+std::to_string(dr.right)+","+std::to_string(dr.bottom);
            Compare(sd.get(),gd.get(),sampling?1:0,label.c_str());
        }
    }
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    auto image=Image(17,13,4,3);
    for(auto sourceRect:{tTVPRect(1,1,12,10),tTVPRect(12,1,1,10),tTVPRect(1,10,12,1),tTVPRect(11,0,0,9)}) {
        auto ss=Create(sw,17,13,TVPTextureFormat::RGBA,image),sd=Create(sw,17,13,TVPTextureFormat::RGBA,image),gd=Create(gpu,17,13,TVPTextureFormat::RGBA,image);
        auto* copy=gpu->GetRenderMethod("Copy");
        Operation(sw,copy,sd.get(),tTVPRect(3,2,14,11),ss.get(),sourceRect);
        Operation(gpu,copy,gd.get(),tTVPRect(3,2,14,11),gd.get(),sourceRect);
        Compare(sd.get(),gd.get(),0,"overlap/flip");
    }
}
static void OffsetUpdates() {
    auto* sw=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    const tTVPRect rectangles[]={tTVPRect(2,1,6,4),tTVPRect(-2,-1,2,2),tTVPRect(7,5,11,8),
        tTVPRect(9,7,13,10),tTVPRect(-5,-4,-1,-1),tTVPRect(2,2,2,5),tTVPRect(1,1,5,1)};
    for(auto format:{TVPTextureFormat::RGBA,TVPTextureFormat::Gray}) for(auto r:rectangles) for(bool pinned:{false,true}) {
        int bpp=format==TVPTextureFormat::Gray?1:4;
        auto image=Image(9,7,bpp,2),frame=Image(4,3,bpp,5);
        auto expected=Create(sw,9,7,format,image),actual=Create(gpu,9,7,format,image);
        if(pinned) actual->GetPersistentCPUData(true);
        expected->Update(frame.data(),format,4*bpp,r);actual->Update(frame.data(),format,4*bpp,r);
        auto reference=image;
        for(int y=0;y<r.get_height();++y) for(int x=0;x<r.get_width();++x) {
            int dx=x+r.left,dy=y+r.top;
            if(dx>=0 && dy>=0 && dx<9 && dy<7)
                std::memcpy(reference.data()+(dy*9+dx)*bpp,frame.data()+(y*4+x)*bpp,bpp);
        }
        for(int y=0;y<7;++y) {
            Require(!std::memcmp(expected->GetScanLineForRead(y),reference.data()+y*9*bpp,9*bpp),"software update did not clip/place frame correctly");
            Require(!std::memcmp(actual->GetScanLineForRead(y),reference.data()+y*9*bpp,9*bpp),"GPU offset update did not clip/place frame correctly");
        }
        ++comparisons;
    }
}
static void Synchronization() {
    auto* gpu=TVPGetRenderManager(); auto image=Image(9,7,4,2);
    auto t=Create(gpu,9,7,TVPTextureFormat::RGBA,image);
    auto before=TVPGetMetalLayerRenderStats();
    auto first=t->GetPoint(0,0);
    auto repeated=t->GetPoint(0,0);
    t->GetPoint(1,0);
    auto afterPoints=TVPGetMetalLayerRenderStats();
    const int pointIndex=static_cast<int>(TVPLayerReadbackSource::Point);
    Require(first==repeated &&
            afterPoints.readbackBytes-before.readbackBytes==8 &&
            afterPoints.readbackCountBySource[pointIndex]-before.readbackCountBySource[pointIndex]==2 &&
            afterPoints.readbackBytesBySource[pointIndex]-before.readbackBytesBySource[pointIndex]==8 &&
            afterPoints.pointCacheHits-before.pointCacheHits==1 &&
            afterPoints.pointCacheMisses-before.pointCacheMisses==2,
            "GPU point cache did not collapse repeated reads");
    t->GetScanLineForRead(0);
    Require(TVPGetMetalLayerRenderStats().readbackBytes-before.readbackBytes==image.size()+8,
            "scanline verification did not perform one full read after point samples");
    auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x10203040);
    Operation(gpu,fill,t.get(),tTVPRect(1,1,4,4),nullptr,tTVPRect());
    Require(t->GetPoint(1,1)==0x10203040 && t->GetPoint(0,0)==first,"GPU write cache invalidation failed");
    {
        Texture loaded(gpu->CreateTexture2D(image.data(),9*4,9,7,TVPTextureFormat::RGBA));
        auto* raw=static_cast<uint32_t*>(loaded->GetPersistentCPUData(false));
        Require(!loaded->IsStatic(),"raw export remained readonly and would relocate on first write");
        loaded->SetPoint(0,0,0x33445566);
        Require(raw==loaded->GetPersistentCPUData(true) && raw[0]==0x33445566,"loaded image raw pointer lifetime changed");
    }
    auto* row=static_cast<uint32_t*>(t->GetScanLineForWrite(0)); row[0]=0xaabbccdd;
    auto output=Create(gpu,9,7,TVPTextureFormat::RGBA,image);
    auto* copy=gpu->GetRenderMethod("Copy");
    Operation(gpu,copy,output.get(),tTVPRect(0,0,9,7),t.get(),tTVPRect(0,0,9,7));
    Require(output->GetPoint(0,0)==0xaabbccdd,"CPU dirty upload failed");
    auto* pointer=static_cast<uint32_t*>(t->GetPersistentCPUData(true));
    pointer[0]=0x55667788;
    Operation(gpu,fill,t.get(),tTVPRect(1,1,4,4),nullptr,tTVPRect());
    Require(pointer==t->GetPersistentCPUData(true) && pointer[10]==0x10203040,"persistent pointer invalidated by fallback");
    pointer[0]=0x99887766;
    Operation(gpu,copy,output.get(),tTVPRect(0,0,9,7),t.get(),tTVPRect(0,0,9,7));
    Require(output->GetPoint(0,0)==0x99887766,"persistent raw writes were lost");
    Texture independent(gpu->CreateTexture2D(9,7,output.get()));
    independent->SetPoint(0,0,0xff123456);
    Require(output->GetPoint(0,0)==0x99887766 && independent->GetPoint(0,0)==0xff123456,"independent copy changed shared pixels");
    auto* gray=gpu->GetRenderMethod("DoGrayScale");
    auto* sw=TVPGetSoftwareRenderManager(); auto expected=Create(sw,9,7,TVPTextureFormat::RGBA,image);
    auto actual=Create(gpu,9,7,TVPTextureFormat::RGBA,image);
    auto beforeFallback=TVPGetMetalLayerRenderStats().cpuFallbacks;
    Operation(sw,gray,expected.get(),tTVPRect(0,0,9,7),expected.get(),tTVPRect(0,0,9,7));
    Operation(gpu,gray,actual.get(),tTVPRect(0,0,9,7),actual.get(),tTVPRect(0,0,9,7));
    Require(TVPGetMetalLayerRenderStats().cpuFallbacks==beforeFallback+1,"unsupported operator did not fall back");
    Compare(expected.get(),actual.get(),0,"software fallback");
    actual->GetTextureHandle();
    Require(actual->GetPoint(0,0)==expected->GetPoint(0,0),"fallback upload cache failed");
}
// A pinned texture used to re-read and re-upload its whole surface on every
// GPU use, so a persistent raw pointer cost a full round trip per frame even
// when nothing changed. Uploads must now be driven by actual damage.
static void ExactHitTestCache() {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    auto image=Image(16,12,4,3), sourceImage=Image(16,12,4,5), coverage=Image(16,12,1,7);
    auto count=[] { return TVPGetMetalLayerRenderStats().readbackCountBySource[
        static_cast<int>(TVPLayerReadbackSource::Point)]; };
    const tTVPRect full(0,0,16,12), corner(0,0,2,2);

    // Unrelated damage must not flush stationary pixel/alpha queries. Include
    // both GPU operators and CPU uploads, with half-open edge coordinates.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        const auto outside=t->GetPoint(2,2), inside=t->GetPoint(1,1);
        auto before=count();
        auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x41203040);
        Operation(gpu,fill,t.get(),corner,nullptr,tTVPRect());
        Require(t->GetPoint(2,2)==outside && t->GetPointAlpha(2,2)==(outside>>24),
                "unrelated GPU damage changed cached pixel");
        Require(count()==before,"unrelated GPU damage triggered point readback");
        Require(t->GetPointAlpha(1,1)==0x41 && count()==before+1,
                "intersecting GPU damage did not refresh alpha");
        const uint32_t changed=inside^0xff000000u;
        t->Update(&changed,TVPTextureFormat::RGBA,4,tTVPRect(1,1,2,2));
        Require(t->GetPoint(2,2)==outside && count()==before+1,
                "unrelated CPU upload triggered point readback");
        Require(t->GetPointAlpha(1,1)==(changed>>24) && count()==before+2,
                "CPU upload left stale alpha");
        t->CommitGPUOverwrite();
        Require(t->GetPointAlpha(2,2)==(outside>>24) && count()==before+3,
                "full GPU overwrite must invalidate every alpha sample");
    }

    // Compare every preserved-alpha formula against production software, then
    // read RGB too: an alpha cache hit must never return stale color bytes.
    const char* preserving[]={"CopyColor","FillColor","AlphaBlend","AlphaBlend_HDA",
        "ConstAlphaBlend_HDA","ApplyColorMap","ConstColorAlphaBlend"};
    for(auto* name:preserving) for(int opacity:{0,127,255}) {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        auto expected=Create(sw,16,12,TVPTextureFormat::RGBA,image);
        auto source=Create(gpu,16,12,TVPTextureFormat::RGBA,sourceImage);
        auto sourceSW=Create(sw,16,12,TVPTextureFormat::RGBA,sourceImage);
        auto mask=Create(gpu,16,12,TVPTextureFormat::Gray,coverage);
        auto maskSW=Create(sw,16,12,TVPTextureFormat::Gray,coverage);
        auto* method=gpu->GetRenderMethod(name);
        method->SetParameterOpa(method->EnumParameterID("opacity"),opacity);
        method->SetParameterColor4B(method->EnumParameterID("color"),0x239e5721);
        TVPLayerOperation op; Require(method->DescribeGpuOperation(op),"missing test operation metadata");
        bool fill=op.kind==TVPLayerOperationKind::FillColor || op.kind==TVPLayerOperationKind::FillBlend;
        bool gray=op.kind==TVPLayerOperationKind::ColorMap;
        auto alpha=t->GetPointAlpha(3,4); auto before=count();
        // Simulate repeated recoloring/blending between pointer hit tests.
        for(int i=0;i<20;++i) {
            Operation(gpu,method,t.get(),full,fill?nullptr:gray?mask.get():source.get(),full);
            Operation(sw,method,expected.get(),full,fill?nullptr:gray?maskSW.get():sourceSW.get(),full);
            Require(t->GetPointAlpha(3,4)==alpha && alpha==expected->GetPointAlpha(3,4),
                    "RGB-only operation changed hit-test alpha");
        }
        Require(count()==before,"RGB-only animation repeatedly read back hit-test alpha");
        const auto actual=t->GetPoint(3,4), wanted=expected->GetPoint(3,4);
        for(int c=0;c<4;++c)
            Require(std::abs(int((actual>>(c*8))&255)-int((wanted>>(c*8))&255))<=1,
                    "alpha cache leaked stale RGB");
        Require(count()==before+1,"RGB query did not refresh after RGB-only animation");
        auto* fillAlpha=gpu->GetRenderMethod("FillMask"); fillAlpha->SetParameterOpa(0,alpha^255);
        Operation(gpu,fillAlpha,t.get(),full,nullptr,tTVPRect());
        Require(t->GetPointAlpha(3,4)==(alpha^255) && count()==before+2,
                "alpha-changing write reused an old alpha-only cache entry");
    }

    // Alpha-writing blend variants, copies and transitions must still query
    // current pixels. The cache must not infer HDA from a method name alone.
    for(auto* name:{"Copy","CopyMask","CopyOpaqueImage","AlphaBlend_d","AlphaBlend_a",
                    "ConstAlphaBlend","ConstAlphaBlend_d","ConstAlphaBlend_a"}) {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        auto expected=Create(sw,16,12,TVPTextureFormat::RGBA,image);
        auto source=Create(gpu,16,12,TVPTextureFormat::RGBA,sourceImage);
        auto sourceSW=Create(sw,16,12,TVPTextureFormat::RGBA,sourceImage);
        t->GetPointAlpha(3,4); auto before=count();
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,127);
        Operation(gpu,method,t.get(),full,source.get(),full);
        Operation(sw,method,expected.get(),full,sourceSW.get(),full);
        Require(t->GetPointAlpha(3,4)==expected->GetPointAlpha(3,4) && count()==before+1,
                "alpha-writing operation retained stale alpha");
    }

    // An existing CPU mirror can seed the sparse cache before GPU ownership;
    // raw CPU writes must remain visible through the alpha accessor.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        t->GetScanLineForRead(0);
        const auto alpha=t->GetPointAlpha(3,4); auto before=count();
        auto* fill=gpu->GetRenderMethod("FillColor"); fill->SetParameterColor4B(0,0x00ffffff);
        Operation(gpu,fill,t.get(),full,nullptr,tTVPRect());
        Require(t->GetPointAlpha(3,4)==alpha && count()==before,"CPU alpha cache was discarded by RGB write");
        auto* row=static_cast<uint32_t*>(t->GetScanLineForWrite(4));
        Require(t->GetPointAlpha(3,4)==alpha,"CPU write lease changed pixels before a write");
        row[3]=0x29553311;
        Operation(gpu,fill,t.get(),full,nullptr,tTVPRect());
        Require(t->GetPointAlpha(3,4)==0x29,
                "query before CPU pointer write survived upload with stale alpha");
        auto* raw=static_cast<uint32_t*>(t->GetPersistentCPUData(true));
        raw[4*16+3]=0x17553311;
        Require(t->GetPointAlpha(3,4)==0x17,"raw CPU alpha write was hidden by cache");
        raw[4*16+3]=0xe6553311;
        Require(t->GetPointAlpha(3,4)==0xe6,"subsequent raw CPU write reused stale alpha");
        Require(t->GetPointAlpha(-1,0)==0 && t->GetPointAlpha(16,0)==0,"alpha query bounds changed");
    }
    ++comparisons;
}
static void PointReadAttribution() {
#ifndef TEST_NATIVE_METAL
    namespace trace=krkrsdl3::point_trace;
    struct Restore {
        bool enabled=trace::Enabled();
        ~Restore() { trace::SetEnabled(enabled); TVPTestCaptureLogs=false; }
    } restore;
    trace::SetEnabled(true); TVPTestCaptureLogs=true; TVPTestLogs.clear(); pointReadObservations.clear();
    auto* gpu=TVPGetRenderManager(); auto image=Image(16,12,4,3);
    auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
    int owner=0;
    trace::TriggerScope trigger(trace::Trigger::InputRecheck);
    trace::OriginScope origin(trace::Source::LayerHitTest,&owner);
    const auto original=t->GetPoint(3,4);
    Require(pointReadObservations.size()==1,"uncached read did not carry query context to backend");
    auto first=pointReadObservations.back();
    Require(first.queryID && first.textureID && first.version && first.origin.source==trace::Source::LayerHitTest &&
            first.origin.trigger==trace::Trigger::InputRecheck && first.origin.owner==reinterpret_cast<uintptr_t>(&owner),
            "point query lost source/trigger/identity");
    Require(first.x==3 && first.y==4 && first.width==16 && first.height==12 && !first.alphaOnly &&
            std::string(first.missReason)=="notCached","initial point query metadata wrong");
    Require(TVPTestLogs.size()==2 && TVPTestLogs.front().find("lastSubmittedID=73 renderFrame=19")!=std::string::npos &&
            TVPTestLogs.front().find("source=layer.hitTest trigger=inputRecheck")!=std::string::npos &&
            TVPTestLogs.back().find("traceState=unavailable")!=std::string::npos,
            "slow query detail/caller logs were not correlated");
    t->GetPointAlpha(3,4);
    Require(pointReadObservations.size()==1 && TVPTestLogs.size()==2,"cache hit emitted a new read trace");

    auto* color=gpu->GetRenderMethod("FillColor"); color->SetParameterColor4B(0,0x00112233);
    Operation(gpu,color,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
    Require(t->GetPointAlpha(3,4)==(original>>24) && pointReadObservations.size()==1,
            "RGB-only mutation invalidated the alpha query");
    t->GetPoint(3,4);
    auto changed=pointReadObservations.back();
    Require(changed.textureID==first.textureID && changed.queryID>first.queryID &&
            changed.invalidation==trace::Invalidation::GPUOperation &&
            std::string(changed.missReason)=="invalidated","RGB invalidation not attributed");

    auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x55112233);
    Operation(gpu,fill,t.get(),tTVPRect(3,4,4,5),nullptr,tTVPRect());
    Operation(gpu,fill,t.get(),tTVPRect(10,8,11,9),nullptr,tTVPRect());
    Require(t->GetPointAlpha(3,4)==0x55,"tracing changed pixel result");
    changed=pointReadObservations.back();
    Require(changed.alphaOnly && changed.invalidatedVersion<changed.version &&
            changed.lastWriteLeft==10 && changed.lastWriteTop==8 && changed.lastWriteRight==11 && changed.lastWriteBottom==9,
            "later unrelated writes were confused with the invalidating write");

    {
        trace::WriterScope writer("emote.captureCanvas");
        t->CommitGPUOverwrite();
    }
    t->GetPointAlpha(3,4);
    changed=pointReadObservations.back();
    Require(changed.invalidation==trace::Invalidation::GPUOverwrite &&
            std::string(changed.writer)=="emote.captureCanvas","Emote overwrite writer lost after scope exit");

    auto* row=static_cast<uint32_t*>(t->GetScanLineForWrite(4));
    t->GetPointAlpha(3,4); // This sample precedes an unannounced pointer write.
    row[3]=0x77112233;
    Operation(gpu,color,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
    Require(t->GetPointAlpha(3,4)==0x77,"CPU pointer write was hidden by diagnostic state");
    Require(pointReadObservations.back().invalidation==trace::Invalidation::CPUUpload,
            "CPU upload invalidation not attributed");
    Require(trace::CurrentQuery()==nullptr,"completed read leaked a query into later operations");
    trace::SetEnabled(false);
    const auto observations=pointReadObservations.size(), logs=TVPTestLogs.size();
    t->InvalidateCPUCache(); t->GetPointAlpha(3,4);
    Require(pointReadObservations.size()==observations && TVPTestLogs.size()==logs,
            "disabled diagnostics still traced backend reads");
    ++comparisons;
#endif
}
static void DirtyRegionUploads() {
    auto* gpu=TVPGetRenderManager(); auto image=Image(64,48,4,7);
    auto t=Create(gpu,64,48,TVPTextureFormat::RGBA,image);
    const size_t surface=size_t(64)*48*4;

    // Pinning alone must not make a clean texture re-upload.
    auto* pointer=static_cast<uint32_t*>(t->GetPersistentCPUData(true));
    t->ReleasePersistentCPUData(nullptr);
    t->GetTextureHandle();
    auto settled=TVPGetMetalLayerRenderStats();
    t->GetTextureHandle(); t->GetTextureHandle();
    auto after=TVPGetMetalLayerRenderStats();
    Require(after.uploadedBytes==settled.uploadedBytes,"pinned texture re-uploaded without any CPU write");
    Require(after.readbackBytes==settled.readbackBytes,"pinned texture re-read without any CPU write");

    // A described write uploads only that region, not the whole surface.
    pointer=static_cast<uint32_t*>(t->GetPersistentCPUData(true));
    pointer[4*64+5]=0x11223344;
    const tTVPRect written(5,4,6,5);
    t->ReleasePersistentCPUData(&written);
    auto beforeSmall=TVPGetMetalLayerRenderStats();
    t->GetTextureHandle();
    auto small=TVPGetMetalLayerRenderStats().uploadedBytes-beforeSmall.uploadedBytes;
    Require(small>0 && small<surface,"described single-pixel write did not narrow the upload");
    Require(t->GetPoint(5,4)==0x11223344,"narrowed upload lost the written pixel");

    // An undescribed lease must stay conservative and re-upload everything.
    pointer=static_cast<uint32_t*>(t->GetPersistentCPUData(true));
    pointer[0]=0x55667788;
    t->ReleasePersistentCPUData(nullptr);
    auto beforeFull=TVPGetMetalLayerRenderStats();
    t->GetTextureHandle();
    Require(TVPGetMetalLayerRenderStats().uploadedBytes-beforeFull.uploadedBytes==surface,"undescribed write did not upload the full surface");
    Require(t->GetPoint(0,0)==0x55667788,"full upload lost the written pixel");

    // Partial Update on a pinned texture reports its own rect.
    auto frame=Image(8,6,4,3);
    t->Update(frame.data(),TVPTextureFormat::RGBA,8*4,tTVPRect(10,10,18,16));
    auto beforePartial=TVPGetMetalLayerRenderStats();
    t->GetTextureHandle();
    auto partial=TVPGetMetalLayerRenderStats().uploadedBytes-beforePartial.uploadedBytes;
    Require(partial>0 && partial<surface,"partial update on a pinned texture uploaded the whole surface");
    Require(t->GetPoint(10,10)==*reinterpret_cast<const uint32_t*>(frame.data()),"partial upload lost frame pixels");

    // Damage pending before a lease opens must survive a narrowed release;
    // otherwise a write far from the lease's own region is silently dropped.
    auto marker=Image(4,4,4,9);
    t->Update(marker.data(),TVPTextureFormat::RGBA,4*4,tTVPRect(40,30,44,34));
    pointer=static_cast<uint32_t*>(t->GetPersistentCPUData(true));
    pointer[20*64+2]=0x0a0b0c0d;
    const tTVPRect leaseWrote(2,20,3,21);
    t->ReleasePersistentCPUData(&leaseWrote);
    t->GetTextureHandle();
    t->InvalidateCPUCache();
    Require(t->GetPoint(40,30)==*reinterpret_cast<const uint32_t*>(marker.data()),"pre-lease damage dropped by narrowed release");
    Require(t->GetPoint(2,20)==0x0a0b0c0d,"lease write lost by narrowed release");
    ++comparisons;
}
// A full-surface writer used to pay a blocking GPU readback for pixels it was
// about to overwrite completely. Such a writer must not read back at all, and
// must still end up with the GPU holding exactly what it wrote.
static void OverwriteSkipsReadback() {
    auto* gpu=TVPGetRenderManager(); auto image=Image(32,24,4,11);
    auto t=Create(gpu,32,24,TVPTextureFormat::RGBA,image);
    // Put the texture in GPU authority so a preserving write would have to read.
    auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x01020304);
    Operation(gpu,fill,t.get(),tTVPRect(0,0,32,24),nullptr,tTVPRect());

    auto before=TVPGetMetalLayerRenderStats();
    auto* raw=static_cast<uint32_t*>(t->GetPersistentCPUDataForOverwrite());
    Require(raw!=nullptr,"overwrite accessor returned no buffer");
    Require(TVPGetMetalLayerRenderStats().readbackBytes==before.readbackBytes,"overwrite path performed a GPU readback");
    for(int i=0;i<32*24;++i) raw[i]=0x0a0b0c0d+i;
    const tTVPRect all(0,0,32,24);
    t->ReleasePersistentCPUData(&all);
    t->GetTextureHandle();

    // Drop the CPU cache so the verification below has to come from the GPU,
    // proving the upload landed rather than reading back what we just wrote.
    t->InvalidateCPUCache();
    auto beforeVerify=TVPGetMetalLayerRenderStats();
    t->GetScanLineForRead(0);
    Require(TVPGetMetalLayerRenderStats().readbackBytes>beforeVerify.readbackBytes,"verification did not re-read from the GPU");
    for(int y=0;y<24;++y) {
        const auto* row=static_cast<const uint32_t*>(t->GetScanLineForRead(y));
        for(int x=0;x<32;++x)
            Require(row[x]==uint32_t(0x0a0b0c0d+y*32+x),"overwritten pixels did not reach the GPU");
    }
    ++comparisons;
}
// Readback totals only become actionable when attributed, so each caller must
// land in its own bucket and the buckets must sum to the total.
static void ReadbackAttribution() {
    auto* gpu=TVPGetRenderManager(); auto image=Image(16,12,4,13);
    auto stats=[]{ return TVPGetMetalLayerRenderStats(); };
    auto bytes=[&](TVPLayerReadbackSource s){ return stats().readbackBytesBySource[static_cast<int>(s)]; };
    auto count=[&](TVPLayerReadbackSource s){ return stats().readbackCountBySource[static_cast<int>(s)]; };
    const size_t surface=size_t(16)*12*4;
    auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x01020304);

    // Single-pixel GPU query: hit testing must not populate the full CPU cache.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        auto before=count(TVPLayerReadbackSource::Point), beforeBytes=bytes(TVPLayerReadbackSource::Point);
        auto statsBefore=stats();
        Require(t->GetPoint(3,4)==0x01020304,"point readback returned the wrong pixel");
        Require(t->GetPoint(3,4)==0x01020304,"cached point returned the wrong pixel");
        auto cached=stats();
        Require(count(TVPLayerReadbackSource::Point)==before+1,"point cache did not suppress duplicate readback");
        Require(bytes(TVPLayerReadbackSource::Point)==beforeBytes+4,"point readback bytes wrong");
        Require(cached.pointCacheHits==statsBefore.pointCacheHits+1 &&
                cached.pointCacheMisses==statsBefore.pointCacheMisses+1,
                "point cache hit/miss counters wrong");
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        Require(t->GetPoint(3,4)==0x01020304,"point cache invalidation returned wrong pixel");
        Require(count(TVPLayerReadbackSource::Point)==before+2,
                "GPU write did not invalidate point cache");
    }
    // Explicit read lock, e.g. hit testing.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        auto before=count(TVPLayerReadbackSource::Lock), beforeBytes=bytes(TVPLayerReadbackSource::Lock);
        t->LockCPURead(); t->UnlockCPU();
        Require(count(TVPLayerReadbackSource::Lock)==before+1,"lock readback not attributed");
        Require(bytes(TVPLayerReadbackSource::Lock)==beforeBytes+surface,"lock readback bytes wrong");
    }
    // Raw persistent pointer handed to a script or plugin.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        auto before=count(TVPLayerReadbackSource::Persistent);
        t->GetPersistentCPUData(false);
        Require(count(TVPLayerReadbackSource::Persistent)==before+1,"persistent readback not attributed");
    }
    // Software fallback for an operator the GPU path cannot take.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        auto before=count(TVPLayerReadbackSource::Fallback);
        auto* gray=gpu->GetRenderMethod("DoGrayScale");
        Operation(gpu,gray,t.get(),tTVPRect(0,0,16,12),t.get(),tTVPRect(0,0,16,12));
        Require(count(TVPLayerReadbackSource::Fallback)>before,"fallback readback not attributed");
    }
    // Scanline access after the GPU took ownership.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        auto before=count(TVPLayerReadbackSource::Pixels);
        t->GetScanLineForRead(0);
        Require(count(TVPLayerReadbackSource::Pixels)==before+1,"pixel readback not attributed");
    }
    // The overwrite path must not appear in any bucket.
    {
        auto t=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
        Operation(gpu,fill,t.get(),tTVPRect(0,0,16,12),nullptr,tTVPRect());
        auto before=stats().readbackBytes;
        t->GetPersistentCPUDataForOverwrite();
        Require(stats().readbackBytes==before,"overwrite path attributed a readback");
    }
    auto final=stats();
    uint64_t sum=0;
    for(int i=0;i<static_cast<int>(TVPLayerReadbackSource::Count);++i) sum+=final.readbackBytesBySource[i];
    Require(sum==final.readbackBytes,"attributed readback bytes do not sum to the total");
    ++comparisons;
}
static void DualSourceTransitions() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    const char* methods[]={"ConstAlphaBlend_SD","ConstAlphaBlend_SD_d"};
    for(auto* name:methods) for(int opacity:{0,1,63,127,128,191,254,255}) {
        auto* method=gpu->GetRenderMethod(name);
        Require(method==sw->GetRenderMethod(name),"dual-source canonical method pointer changed");
        method->SetParameterOpa(method->EnumParameterID("opacity"),opacity);
        TVPLayerOperation op;
        Require(method->DescribeGpuOperation(op) && op.kind==TVPLayerOperationKind::ConstAlphaSD,
                "dual-source semantic descriptor missing");

        auto first=Image(17,13,4,2),second=Image(17,13,4,5),initial=Image(17,13,4,7);
        tTVPRect dr(3,2,14,11),sr1(1,1,12,10),sr2(2,2,13,11);

        // Independent sources -> independent target.
        {
            auto s1=Create(sw,17,13,TVPTextureFormat::RGBA,first);
            auto s2=Create(sw,17,13,TVPTextureFormat::RGBA,second);
            auto expected=Create(sw,17,13,TVPTextureFormat::RGBA,initial);
            std::pair<iTVPTexture2D*,tTVPRect> si[]={{s1.get(),sr1},{s2.get(),sr2}};
            sw->OperateRect(method,expected.get(),nullptr,dr,tRenderTexRectArray(si));

            auto g1=Create(gpu,17,13,TVPTextureFormat::RGBA,first);
            auto g2=Create(gpu,17,13,TVPTextureFormat::RGBA,second);
            auto actual=Create(gpu,17,13,TVPTextureFormat::RGBA,initial);
            std::pair<iTVPTexture2D*,tTVPRect> gi[]={{g1.get(),sr1},{g2.get(),sr2}};
            auto before=TVPGetMetalLayerRenderStats();
            gpu->OperateRect(method,actual.get(),nullptr,dr,tRenderTexRectArray(gi));
            auto after=TVPGetMetalLayerRenderStats();
            Require(after.gpuOperations==before.gpuOperations+1 &&
                    after.cpuFallbacks==before.cpuFallbacks &&
                    after.readbackBytes==before.readbackBytes,
                    "dual-source transition fell back/read back");
            Compare(expected.get(),actual.get(),0,name);
        }

        // Common KRKR pattern: second input aliases the output target at
        // the same coordinates. This is one-pixel-to-one-pixel and can be
        // snapshotted without changing software semantics.
        {
            auto s1=Create(sw,17,13,TVPTextureFormat::RGBA,first);
            auto expected=Create(sw,17,13,TVPTextureFormat::RGBA,second);
            std::pair<iTVPTexture2D*,tTVPRect> si[]={{s1.get(),sr1},{expected.get(),dr}};
            sw->OperateRect(method,expected.get(),expected.get(),dr,tRenderTexRectArray(si));

            auto g1=Create(gpu,17,13,TVPTextureFormat::RGBA,first);
            auto actual=Create(gpu,17,13,TVPTextureFormat::RGBA,second);
            std::pair<iTVPTexture2D*,tTVPRect> gi[]={{g1.get(),sr1},{actual.get(),dr}};
            auto before=TVPGetMetalLayerRenderStats();
            gpu->OperateRect(method,actual.get(),actual.get(),dr,tRenderTexRectArray(gi));
            auto after=TVPGetMetalLayerRenderStats();
            Require(after.gpuOperations==before.gpuOperations+1 &&
                    after.cpuFallbacks==before.cpuFallbacks &&
                    after.readbackBytes==before.readbackBytes,
                    "same-rect aliased dual-source transition fell back/read back");
            Compare(expected.get(),actual.get(),0,(std::string(name)+" alias").c_str());
        }

        // Offset overlap has order-dependent software semantics. It must stay
        // on software rather than being silently changed by a GPU snapshot.
        if(opacity==127) {
            auto s1=Create(sw,17,13,TVPTextureFormat::RGBA,first);
            auto expected=Create(sw,17,13,TVPTextureFormat::RGBA,second);
            std::pair<iTVPTexture2D*,tTVPRect> si[]={{s1.get(),sr1},{expected.get(),sr2}};
            sw->OperateRect(method,expected.get(),expected.get(),dr,tRenderTexRectArray(si));

            auto g1=Create(gpu,17,13,TVPTextureFormat::RGBA,first);
            auto actual=Create(gpu,17,13,TVPTextureFormat::RGBA,second);
            std::pair<iTVPTexture2D*,tTVPRect> gi[]={{g1.get(),sr1},{actual.get(),sr2}};
            auto before=TVPGetMetalLayerRenderStats();
            gpu->OperateRect(method,actual.get(),actual.get(),dr,tRenderTexRectArray(gi));
            auto after=TVPGetMetalLayerRenderStats();
            Require(after.cpuFallbacks==before.cpuFallbacks+1,
                    "offset alias unexpectedly used GPU path");
            Compare(expected.get(),actual.get(),0,(std::string(name)+" offset alias fallback").c_str());
        }
    }
}
static void UnivTransSetParameters(iTVPRenderMethod* method,int phase,int vague) {
    method->SetParameterInt(method->EnumParameterID("vague"),vague);
    method->SetParameterInt(method->EnumParameterID("phase"),phase);
}
static void UnivTransReference(iTVPRenderMethod* method,iTVPTexture2D* target,
                               const tTVPRect& dst,iTVPTexture2D* s1,tTVPRect src1,
                               iTVPTexture2D* s2,tTVPRect src2,iTVPTexture2D* rule,tTVPRect rr) {
    // The software three-input primitive expects pre-clipped rectangles. Apply
    // the target clip and translate ALL source origins for the independent oracle.
    tTVPRect clip(std::max(0,dst.left),std::max(0,dst.top),
                  std::min(int(target->GetWidth()),dst.right),std::min(int(target->GetHeight()),dst.bottom));
    if(clip.get_width()<=0 || clip.get_height()<=0) return;
    for(auto* r:{&src1,&src2,&rr}) {
        r->left+=clip.left-dst.left; r->top+=clip.top-dst.top;
        r->right=r->left+clip.get_width(); r->bottom=r->top+clip.get_height();
    }
    std::pair<iTVPTexture2D*,tTVPRect> inputs[]={{s1,src1},{s2,src2},{rule,rr}};
    TVPGetSoftwareRenderManager()->OperateRect(method,target,nullptr,clip,tRenderTexRectArray(inputs));
}
static void UnivTransGPU(iTVPRenderMethod* method,iTVPTexture2D* target,const tTVPRect& dst,
                         iTVPTexture2D* s1,const tTVPRect& src1,iTVPTexture2D* s2,const tTVPRect& src2,
                         iTVPTexture2D* rule,const tTVPRect& rr,bool snapshot=false) {
    // Upload initial data before measuring. A resident operation must perform
    // no further upload or readback, even when either RGBA source aliases output.
    for(auto* t:{target,s1,s2,rule}) t->GetTextureHandle();
    auto before=TVPGetMetalLayerRenderStats();
#ifdef TEST_NATIVE_METAL
    auto submits=testMetalSubmits,waits=testMetalWaits,blits=testMetalBlits;
#else
    (void)snapshot;
#endif
    std::pair<iTVPTexture2D*,tTVPRect> inputs[]={{s1,src1},{s2,src2},{rule,rr}};
    TVPGetRenderManager()->OperateRect(method,target,nullptr,dst,tRenderTexRectArray(inputs));
    auto after=TVPGetMetalLayerRenderStats();
    Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks,
            "UnivTrans did not stay on GPU");
    Require(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
            "resident UnivTrans performed a CPU transfer");
    Require(after.gpuRejectCountByReason[static_cast<int>(TVPLayerGPURejectReason::MultipleInputs)]==
            before.gpuRejectCountByReason[static_cast<int>(TVPLayerGPURejectReason::MultipleInputs)],
            "UnivTrans rejected three inputs");
#ifdef TEST_NATIVE_METAL
    Require(testMetalSubmits==submits && testMetalWaits==waits,"UnivTrans submitted or waited synchronously");
    if(snapshot) Require(testMetalBlits>blits,"aliased UnivTrans did not snapshot on GPU");
#endif
}
static void UnivTransTransitions() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    constexpr int w=33,h=21;
    auto first=Image(w,h,4,2),second=Image(w,h,4,5),initial=Image(w,h,4,7);
    uint32_t random=0x118ac731;
    for(size_t i=0;i<first.size();++i) {
        random=random*1664525u+1013904223u;
        if(i%4!=3) { first[i]=uint8_t(random>>24); second[i]=uint8_t(random>>16); }
    }
    std::vector<uint8_t> rules(w*h);
    for(size_t i=0;i<rules.size();++i) rules[i]=uint8_t(i); // full 0..255 coverage
    const char* names[]={"UnivTransBlend","UnivTransBlend_d","UnivTransBlend_a"};
    unsigned cases=0;
    for(auto* name:names) {
        auto* method=gpu->GetRenderMethod(name);
        Require(method==sw->GetRenderMethod(name),"UnivTrans canonical method pointer changed");
        auto ss1=Create(sw,w,h,TVPTextureFormat::RGBA,first),ss2=Create(sw,w,h,TVPTextureFormat::RGBA,second);
        auto sr=Create(sw,w,h,TVPTextureFormat::Gray,rules);
        auto gs1=Create(gpu,w,h,TVPTextureFormat::RGBA,first),gs2=Create(gpu,w,h,TVPTextureFormat::RGBA,second);
        auto gr=Create(gpu,w,h,TVPTextureFormat::Gray,rules);
        for(int vague:{0,1,16,64,255,511,512,1024}) for(int phase:{0,1,63,127,255,256,255+vague}) {
            UnivTransSetParameters(method,phase,vague);
            TVPLayerOperation op;
            const uint32_t flags=std::string(name)=="UnivTransBlend_d" ? TVP_LAYER_DEST_ALPHA :
                std::string(name)=="UnivTransBlend_a" ? TVP_LAYER_DEST_PREMULTIPLIED : 0;
            Require(method->DescribeGpuOperation(op) && op.kind==TVPLayerOperationKind::UnivTrans &&
                    op.phase==phase && op.vague==vague && op.flags==flags,"UnivTrans semantic parameters stale");
            auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
            auto actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
            tTVPRect rect(0,0,w,h);
            UnivTransReference(method,expected.get(),rect,ss1.get(),rect,ss2.get(),rect,sr.get(),rect);
            UnivTransGPU(method,actual.get(),rect,gs1.get(),rect,gs2.get(),rect,gr.get(),rect);
            Compare(expected.get(),actual.get(),0,name); ++cases;
        }
        // Non-zero, independent source/rule origins; odd sizes, 1x1, every
        // clipping edge and an empty target intersection.
        for(int vague:{64,512}) for(auto dr:{tTVPRect(3,2,24,19),tTVPRect(-2,-1,17,14),
                                            tTVPRect(25,15,40,26),tTVPRect(4,6,5,7),tTVPRect(-9,-8,-2,-3)}) {
            UnivTransSetParameters(method,127,vague);
            int dw=dr.get_width(),dh=dr.get_height();
            tTVPRect r1(1,1,1+dw,1+dh),r2(3,2,3+dw,2+dh),rr(6,1,6+dw,1+dh);
            auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
            auto actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
            UnivTransReference(method,expected.get(),dr,ss1.get(),r1,ss2.get(),r2,sr.get(),rr);
            UnivTransGPU(method,actual.get(),dr,gs1.get(),r1,gs2.get(),r2,gr.get(),rr);
            Compare(expected.get(),actual.get(),0,"UnivTrans clip/origins"); ++cases;
        }
        // Both target aliases and source1==source2. References use immutable
        // input copies so offset overlaps exercise snapshot-before-write semantics.
        for(int vague:{64,512}) for(int alias:{1,2,3,4}) for(bool offset:{false,true}) {
            UnivTransSetParameters(method,127,vague);
            tTVPRect dr(4,4,17,13),r1=offset?tTVPRect(2,3,15,12):dr,
                r2=offset?tTVPRect(5,5,18,14):dr,rr(7,1,20,10);
            auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
            auto actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
            auto snap1=Create(sw,w,h,TVPTextureFormat::RGBA,(alias==1 || alias==3)?initial:first);
            auto snap2=Create(sw,w,h,TVPTextureFormat::RGBA,(alias==2 || alias==3)?initial:(alias==4?first:second));
            UnivTransReference(method,expected.get(),dr,snap1.get(),r1,snap2.get(),r2,sr.get(),rr);
            UnivTransGPU(method,actual.get(),dr,(alias==1 || alias==3)?actual.get():gs1.get(),r1,
                (alias==2 || alias==3)?actual.get():(alias==4?gs1.get():gs2.get()),r2,gr.get(),rr,alias!=4);
            Compare(expected.get(),actual.get(),0,"UnivTrans alias"); ++cases;
        }
        // Reuse alias snapshots without readback/submission between dispatches.
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
        auto actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        tTVPRect dr(4,4,17,13),rr(7,1,20,10);
        for(int phase:{63,127,191}) {
            UnivTransSetParameters(method,phase,64);
            std::vector<uint8_t> snapshot(size_t(w)*h*4);
            for(int y=0;y<h;++y) std::memcpy(snapshot.data()+y*w*4,expected->GetScanLineForRead(y),w*4);
            auto prior=Create(sw,w,h,TVPTextureFormat::RGBA,snapshot);
            UnivTransReference(method,expected.get(),dr,prior.get(),dr,ss2.get(),dr,sr.get(),rr);
            UnivTransGPU(method,actual.get(),dr,actual.get(),dr,gs2.get(),dr,gr.get(),rr,true);
        }
        Compare(expected.get(),actual.get(),0,"UnivTrans batched snapshots"); ++cases;

        // Snapshot reallocation and clipped destination offsets in one batch.
        expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
        actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        for(auto clipped:{tTVPRect(-2,-1,13,10),tTVPRect(25,15,36,24)}) {
            UnivTransSetParameters(method,127,512);
            int dw=clipped.get_width(),dh=clipped.get_height();
            tTVPRect r1(1,2,1+dw,2+dh),r2(3,4,3+dw,4+dh),ruleRect(6,3,6+dw,3+dh);
            std::vector<uint8_t> snapshot(size_t(w)*h*4);
            for(int y=0;y<h;++y) std::memcpy(snapshot.data()+y*w*4,expected->GetScanLineForRead(y),w*4);
            auto prior=Create(sw,w,h,TVPTextureFormat::RGBA,snapshot);
            UnivTransReference(method,expected.get(),clipped,prior.get(),r1,prior.get(),r2,sr.get(),ruleRect);
            UnivTransGPU(method,actual.get(),clipped,actual.get(),r1,actual.get(),r2,gr.get(),ruleRect,true);
        }
        Compare(expected.get(),actual.get(),0,"UnivTrans clipped/batched alias resize"); ++cases;
    }
    std::cout<<"PASS UnivTrans three-source routing/geometry/alias: "<<cases<<" exact surface comparisons\n";
}
static void UnivTransFallbacks(iTVPRenderBackend* backend) {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    auto* method=gpu->GetRenderMethod("UnivTransBlend_d"); UnivTransSetParameters(method,127,64);
    auto first=Image(9,7,4,2),second=Image(9,7,4,5),initial=Image(9,7,4,7),rules=Image(9,7,1,3);
    tTVPRect rect(0,0,9,7);
#ifdef TEST_NATIVE_METAL
    (void)backend;
#endif
    auto ss1=Create(sw,9,7,TVPTextureFormat::RGBA,first),ss2=Create(sw,9,7,TVPTextureFormat::RGBA,second);
    auto gs1=Create(gpu,9,7,TVPTextureFormat::RGBA,first),gs2=Create(gpu,9,7,TVPTextureFormat::RGBA,second);
    for(int failure:{0,1,2,3}) {
#ifdef TEST_NATIVE_METAL
        // Missing pipeline is injected in the device double; native creation
        // failure is handled by the optional backend API's false result.
        if(failure==2) continue;
#else
        auto* device=dynamic_cast<DeviceDouble*>(backend); Require(device,"expected device double");
        device->rejectTripleSource=failure==2;
#endif
        bool badFormat=failure==0;
        auto format=badFormat?TVPTextureFormat::RGBA:TVPTextureFormat::Gray;
        auto data=badFormat?Image(9,7,4,3):rules;
        auto sr=Create(sw,9,7,format,data),gr=Create(gpu,9,7,format,data);
        auto expected=Create(sw,9,7,TVPTextureFormat::RGBA,initial),actual=Create(gpu,9,7,TVPTextureFormat::RGBA,initial);
        // The rule's declared right edge is wrong, but its actual allocation
        // still covers the software primitive's 9 pixels; fallback is safe.
        tTVPRect rr=failure==1?tTVPRect(0,0,8,7):rect;
        std::pair<iTVPTexture2D*,tTVPRect> si[]={{ss1.get(),rect},{ss2.get(),rect},{sr.get(),rr},{sr.get(),rect}};
        std::pair<iTVPTexture2D*,tTVPRect> gi[]={{gs1.get(),rect},{gs2.get(),rect},{gr.get(),rr},{gr.get(),rect}};
        for(auto* t:{actual.get(),gs1.get(),gs2.get(),gr.get()}) t->GetTextureHandle();
        // Make target CPU cache stale, so fallback must recover GPU pixels.
        Operation(gpu,gpu->GetRenderMethod("Copy"),actual.get(),rect,gs1.get(),rect);
        expected=Create(sw,9,7,TVPTextureFormat::RGBA,first);
        size_t count=failure==3?4:3;
        sw->OperateRect(method,expected.get(),nullptr,rect,tRenderTexRectArray(si,count));
        auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateRect(method,actual.get(),nullptr,rect,tRenderTexRectArray(gi,count));
        auto after=TVPGetMetalLayerRenderStats();
        auto reason=failure==0?TVPLayerGPURejectReason::SourceFormat:failure==1?TVPLayerGPURejectReason::InvalidGeometry:
            failure==2?TVPLayerGPURejectReason::BackendFailure:TVPLayerGPURejectReason::MultipleInputs;
        Require(after.cpuFallbacks==before.cpuFallbacks+1 && after.gpuOperations==before.gpuOperations &&
                after.gpuRejectCountByReason[int(reason)]==before.gpuRejectCountByReason[int(reason)]+1,
                "UnivTrans failure not safely attributed to software");
        Compare(expected.get(),actual.get(),0,"UnivTrans fallback");
        // The failed path must leave a valid GPU upload for later operations.
        actual->GetTextureHandle();
#ifndef TEST_NATIVE_METAL
        device->rejectTripleSource=false;
#endif
    }
}
static void UnivTransClippedFallbacks(iTVPRenderBackend* backend) {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    constexpr int swidth=33,sheight=21,w=9,h=7;
    auto first=Image(swidth,sheight,4,2),second=Image(swidth,sheight,4,5);
    auto rules=Image(swidth,sheight,1,3),initial=Image(w,h,4,7);
    auto ss1=Create(sw,swidth,sheight,TVPTextureFormat::RGBA,first);
    auto ss2=Create(sw,swidth,sheight,TVPTextureFormat::RGBA,second);
    auto sr=Create(sw,swidth,sheight,TVPTextureFormat::Gray,rules);
    auto gs1=Create(gpu,swidth,sheight,TVPTextureFormat::RGBA,first);
    auto gs2=Create(gpu,swidth,sheight,TVPTextureFormat::RGBA,second);
    auto gr=Create(gpu,swidth,sheight,TVPTextureFormat::Gray,rules);
    unsigned cases=0;
    for(auto* name:{"UnivTransBlend","UnivTransBlend_d","UnivTransBlend_a"})
    for(int vague:{64,512}) for(int failure:{0,1,2}) {
#ifdef TEST_NATIVE_METAL
        (void)backend;
        if(failure==2) continue; // Pipeline rejection is injected by the double.
#else
        auto* device=dynamic_cast<DeviceDouble*>(backend); Require(device,"expected device double");
        device->rejectTripleSource=failure==2;
#endif
        auto* method=gpu->GetRenderMethod(name); UnivTransSetParameters(method,127,vague);
        for(auto dr:{tTVPRect(7,5,12,9),tTVPRect(-2,1,5,6),tTVPRect(1,-2,8,4),
                     tTVPRect(-1,-1,12,10),tTVPRect(3,2,4,3),tTVPRect(12,9,16,12),
                     tTVPRect(-9,-8,-2,-3)}) {
            // Guarded software storage makes an overrun a deterministic failure
            // without corrupting the heap in the right/bottom clipping case.
            // Negative-origin cases exercise pinned GPU caches instead.
            if(failure==0 && (dr.left<0 || dr.top<0)) continue;
            int dw=dr.get_width(),dh=dr.get_height();
            tTVPRect r1(1,1,1+dw,1+dh),r2(3,2,3+dw,2+dh),rr(6,1,6+dw,1+dh);
            auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
            std::vector<uint8_t> guarded(initial.size()+w*8*4,0xa5);
            std::copy(initial.begin(),initial.end(),guarded.begin());
            Texture actual;
            if(failure==0) actual.reset(sw->CreateTexture2D(guarded.data(),w*4,w,h,TVPTextureFormat::RGBA));
            else {
                actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
                // Invalidate CPU pixels before fallback; an empty intersection
                // must leave these GPU contents resident without a readback.
                Operation(gpu,gpu->GetRenderMethod("Copy"),actual.get(),tTVPRect(0,0,w,h),
                          actual.get(),tTVPRect(0,0,w,h));
                if(failure==1) actual->GetPersistentCPUData(false);
            }
            Operation(gpu,gpu->GetRenderMethod("Copy"),gs1.get(),tTVPRect(0,0,swidth,sheight),
                      gs1.get(),tTVPRect(0,0,swidth,sheight));
            UnivTransReference(method,expected.get(),dr,ss1.get(),r1,ss2.get(),r2,sr.get(),rr);
            std::pair<iTVPTexture2D*,tTVPRect> inputs[]={{gs1.get(),r1},{gs2.get(),r2},{gr.get(),rr}};
            auto before=TVPGetMetalLayerRenderStats();
            gpu->OperateRect(method,actual.get(),nullptr,dr,tRenderTexRectArray(inputs));
            auto after=TVPGetMetalLayerRenderStats();
            bool empty=dr.right<=0 || dr.bottom<=0 || dr.left>=w || dr.top>=h;
            Require(after.gpuOperations==before.gpuOperations &&
                    after.cpuFallbacks==before.cpuFallbacks+(empty?0:1),"clipped UnivTrans fallback count incorrect");
            if(empty) Require(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                              "empty UnivTrans fallback transferred pixels");
            Require(std::all_of(guarded.begin()+initial.size(),guarded.end(),[](uint8_t b){return b==0xa5;}),
                    "UnivTrans fallback wrote beyond the target surface");
            Compare(expected.get(),actual.get(),0,"UnivTrans clipped fallback"); ++cases;
            // Reusing the GPU handle uploads only the visible output region.
            actual->GetTextureHandle();
            const uint64_t upload=failure==0 || empty ? 0 :
                uint64_t(std::min(w,dr.right)-std::max(0,dr.left))*
                (std::min(h,dr.bottom)-std::max(0,dr.top))*4;
            Require(TVPGetMetalLayerRenderStats().uploadedBytes==after.uploadedBytes+upload,
                    "UnivTrans fallback uploaded outside the visible output region");
        }
#ifndef TEST_NATIVE_METAL
        device->rejectTripleSource=false;
#endif
    }
    std::cout<<"PASS UnivTrans clipped/empty software fallbacks: "<<cases<<" exact surface comparisons\n";
}
static void Compatibility() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    auto image=Image(9,7,4,2),second=Image(9,7,4,5);
    auto expected=Create(sw,9,7,TVPTextureFormat::RGBA,second),actual=Create(gpu,9,7,TVPTextureFormat::RGBA,second);
    auto ss=Create(sw,9,7,TVPTextureFormat::RGBA,image),gs=Create(gpu,9,7,TVPTextureFormat::RGBA,image);
    auto* transition=gpu->GetRenderMethod("AlphaBlend_SD"); transition->SetParameterOpa(0,127);
    std::pair<iTVPTexture2D*,tTVPRect> si[]={{ss.get(),tTVPRect(0,0,9,7)},{expected.get(),tTVPRect(0,0,9,7)}};
    std::pair<iTVPTexture2D*,tTVPRect> gi[]={{gs.get(),tTVPRect(0,0,9,7)},{actual.get(),tTVPRect(0,0,9,7)}};
    sw->OperateRect(transition,expected.get(),expected.get(),tTVPRect(0,0,9,7),tRenderTexRectArray(si));
    gpu->OperateRect(transition,actual.get(),actual.get(),tTVPRect(0,0,9,7),tRenderTexRectArray(gi));
    Compare(expected.get(),actual.get(),0,"transition target alias fallback");
    auto* copy=gpu->GetRenderMethod("Copy");
    tTVPPointD triangle[]={{1,1},{7,1},{1,5},{7,1},{1,5},{7,5}};
    std::pair<iTVPTexture2D*,const tTVPPointD*> st(ss.get(),triangle),gt(gs.get(),triangle);
    sw->OperateTriangles(copy,2,expected.get(),nullptr,tTVPRect(0,0,9,7),triangle,tRenderTexQuadArray(&st,1));
    gpu->OperateTriangles(copy,2,actual.get(),nullptr,tTVPRect(0,0,9,7),triangle,tRenderTexQuadArray(&gt,1));
    Compare(expected.get(),actual.get(),0,"affine fallback");
    tTVPPointD quad[]={{1,1},{7,1},{1,5},{7,5}};
    st.second=quad;gt.second=quad;
    sw->OperatePerspective(copy,1,expected.get(),nullptr,tTVPRect(0,0,9,7),quad,tRenderTexQuadArray(&st,1));
    gpu->OperatePerspective(copy,1,actual.get(),nullptr,tTVPRect(0,0,9,7),quad,tRenderTexQuadArray(&gt,1));
    Compare(expected.get(),actual.get(),0,"perspective fallback");
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),3);
    Operation(sw,copy,expected.get(),tTVPRect(1,1,8,6),ss.get(),tTVPRect(0,0,9,7));
    Operation(gpu,copy,actual.get(),tTVPRect(1,1,8,6),gs.get(),tTVPRect(0,0,9,7));
    Compare(expected.get(),actual.get(),0,"unsupported sampler fallback");
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    // Province resources remain CPU owned regardless of the current manager.
    auto province=Create(sw,9,7,TVPTextureFormat::Gray,Image(9,7,1,2));
    Require(province->IsCPUResident(),"Province texture unexpectedly on GPU");
    Require(gpu->GetRenderMethod(127,true,0)!=nullptr,"shared method cache not initialized");
}
static void CompositionWorkload() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    auto source=Image(320,180,4,2),destination=Image(320,180,4,4);
    auto ss=Create(sw,320,180,TVPTextureFormat::RGBA,source),gs=Create(gpu,320,180,TVPTextureFormat::RGBA,source);
    auto sd=Create(sw,320,180,TVPTextureFormat::RGBA,destination),gd=Create(gpu,320,180,TVPTextureFormat::RGBA,destination);
    auto* method=gpu->GetRenderMethod("AlphaBlend_d");method->SetParameterOpa(0,191);
    auto workload=[&](iTVPRenderManager* manager,iTVPTexture2D* target,iTVPTexture2D* input) {
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<120;++i) Operation(manager,method,target,tTVPRect(0,0,320,180),input,tTVPRect(0,0,320,180));
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    double cpu=workload(sw,sd.get(),ss.get());auto before=TVPGetMetalLayerRenderStats();
    double gpuCPU=workload(gpu,gd.get(),gs.get());auto after=TVPGetMetalLayerRenderStats();
    Require(after.gpuOperations-before.gpuOperations==120 && after.cpuFallbacks==before.cpuFallbacks && after.readbackBytes==before.readbackBytes,"composition workload used CPU/readbacks");
    Compare(sd.get(),gd.get(),1,"composition workload");
    std::cout<<"composition CPU wall ms: software="<<cpu<<" GPU encoding="<<gpuCPU<<" (120 operations)\n";
}

static void GlyphWorkload() {
    auto* sw=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    auto glyph=Image(16,16,1,2),image=Image(320,180,4,4);
    auto ss=Create(sw,16,16,TVPTextureFormat::Gray,glyph),gs=Create(gpu,16,16,TVPTextureFormat::Gray,glyph);
    auto sd=Create(sw,320,180,TVPTextureFormat::RGBA,image),gd=Create(gpu,320,180,TVPTextureFormat::RGBA,image);
    auto* method=gpu->GetRenderMethod("ApplyColorMap_d");method->SetParameterOpa(0,191);method->SetParameterColor4B(1,0x91eab532);
    auto workload=[&](iTVPRenderManager* manager,iTVPTexture2D* target,iTVPTexture2D* source) {
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<320;++i) { int x=(i%19)*16,y=((i/19)%10)*16; Operation(manager,method,target,tTVPRect(x,y,x+16,y+16),source,tTVPRect(0,0,16,16)); }
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    double cpu=workload(sw,sd.get(),ss.get());auto before=TVPGetMetalLayerRenderStats();
    double gpuCPU=workload(gpu,gd.get(),gs.get());auto after=TVPGetMetalLayerRenderStats();
    Require(after.gpuOperations-before.gpuOperations==320 && after.cpuFallbacks==before.cpuFallbacks && after.readbackBytes==before.readbackBytes,"glyph workload used CPU/readbacks");
    Compare(sd.get(),gd.get(),1,"glyph workload");
    std::cout<<"glyph CPU wall ms: software="<<cpu<<" GPU encoding="<<gpuCPU<<" (320 glyphs)\n";
}
#ifdef TEST_NATIVE_METAL
static void Presentation(iTVPRenderBackend* backend) {
    auto* gpu=TVPGetRenderManager(); Texture t(gpu->CreateTexture2D(nullptr,0,4,4,TVPTextureFormat::RGBA));
    auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x00204080);
    Operation(gpu,fill,t.get(),tTVPRect(0,0,4,4),nullptr,tTVPRect());
    auto before=TVPGetMetalLayerRenderStats();void* handle=t->GetTextureHandle();
    Require(handle && backend->GetTargetTexture(handle)==handle,"GPU presentation alias failed");
    backend->BeginFrame(128,128);backend->DrawWindowTexture(handle,0,0,128,128);backend->EndFrame();
    Require(TVPGetMetalLayerRenderStats().readbackBytes==before.readbackBytes,"presentation read CPU pixels");
    std::vector<uint8_t> screenshot;int width=0,height=0,pitch=0;
    Require(backend->CaptureFrame(screenshot,width,height,pitch),"GPU screenshot failed");
    size_t center=size_t(height/2)*pitch+(width/2)*4;
    Require(screenshot[center]==0x80 && screenshot[center+1]==0x40 && screenshot[center+2]==0x20 && screenshot[center+3]==255,"screenshot did not preserve visible RGB");
}
#endif

int main(int argc,char** argv) {
    try {
        TVPInitTVPGL(); TVPGetRenderManager(ttstr("software"));
        UnivTransShaderTests();
        std::unique_ptr<iTVPRenderBackend> backend;
#ifdef TEST_NATIVE_METAL
        Require(SDL_Init(SDL_INIT_VIDEO),"SDL video init failed");
        SDL_Window* window=SDL_CreateWindow("Metal Layer tests",128,128,SDL_WINDOW_METAL|SDL_WINDOW_HIDDEN);
        if(!window) { std::cout<<"SKIP no native Metal window\n"; SDL_Quit(); return 77; }
        backend.reset(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(bool(backend),"native Metal init failed");
#else
        backend=std::make_unique<DeviceDouble>();
#endif
#ifndef TEST_NATIVE_METAL
        class UnavailableDevice final : public DeviceDouble {
            void* CreateLayerTexture(int,int,TVPLayerTextureFormat) override { return nullptr; }
        } unavailable;
        Require(!TVPBindMetalLayerRenderManager(&unavailable) && TVPIsSoftwareRenderManager() && std::strlen(TVPMetalLayerFallbackReason()),"resource init did not retain software composition");
#endif
        if(argc>1 && std::string(argv[1])=="--performance") {
            Require(TVPBindMetalLayerRenderManager(backend.get()),"GPU Layer init failed");
            CompositionWorkload(); GlyphWorkload(); TVPUnbindMetalLayerRenderManager(); backend.reset();
#ifdef TEST_NATIVE_METAL
            SDL_DestroyWindow(window);SDL_Quit();
#endif
            return 0;
        }
        auto* cached=TVPGetSoftwareRenderManager()->GetRenderMethod("AlphaBlend_d");
        for(int session=0;session<3;++session) {
            TVPSetMetalLayerTriangleDiagnostics(true);
            Require(TVPBindMetalLayerRenderManager(backend.get()),"GPU Layer init failed");
            const auto newTriangleInterval=TVPTakeMetalLayerTriangleProfile();
            Require(newTriangleInterval.stats.calls==0 && newTriangleInterval.stats.maxCpuTimeNS==0 &&
                    newTriangleInterval.methods.empty(),"triangle profile leaked across Layer sessions");
            Require(cached==TVPGetRenderManager()->GetRenderMethod("AlphaBlend_d"),"method lifetime changed");
            {
                auto pixels=Image(9,7,4,2);auto texture=Create(TVPGetRenderManager(),9,7,TVPTextureFormat::RGBA,pixels);
                std::vector<uint8_t> region;int pitch=0;
                Require(backend->ReadLayerTextureRegion(texture->GetTextureHandle(),TVPLayerRect{2,3,5,5},region,pitch) && pitch==12 && region.size()==24,"local RGBA readback failed");
                for(int y=0;y<2;++y) Require(!std::memcmp(region.data()+y*pitch,pixels.data()+((y+3)*9+2)*4,12),"local readback pixels differ");
            }
            Equivalence(); DualSourceTransitions(); UnivTransTransitions(); UnivTransFallbacks(backend.get()); UnivTransClippedFallbacks(backend.get()); OffsetUpdates(); Synchronization(); ExactHitTestCache(); PointReadAttribution(); DirtyRegionUploads(); OverwriteSkipsReadback(); BitmapOverwriteTests(backend.get()); ReadbackAttribution(); Compatibility(); TriangleProfileTests(); CompositionWorkload(); GlyphWorkload();
#ifdef TEST_NATIVE_METAL
            Presentation(backend.get());
#endif
            iTVPTexture2D::RecycleProcess();
            Require(TVPGetMetalLayerRenderStats().gpuResidentBytes==0,"session GPU texture leak");
            TVPUnbindMetalLayerRenderManager(); Require(TVPIsSoftwareRenderManager(),"software manager not restored");
            Require(TVPTakeMetalLayerTriangleProfile().stats.calls==0,"triangle sampler retained an unbound Layer session");
        }
        // Deliberately retained texture is detached safely when its session ends.
        Require(TVPBindMetalLayerRenderManager(backend.get()),"rebind failed");
        auto survivor=Create(TVPGetRenderManager(),2,2,TVPTextureFormat::RGBA,Image(2,2,4,1));
        uint32_t pixel=survivor->GetPoint(0,0); TVPUnbindMetalLayerRenderManager();backend.reset();
        Require(survivor->IsCPUResident() && survivor->GetPoint(0,0)==pixel,"texture outlived backend unsafely");
        uint64_t memory=0;
        Require(TVPGetRenderManager()->GetTextureStat(survivor.get(),memory) && memory==16,"detached texture incompatible with software stats");
        auto* fill=TVPGetRenderManager()->GetRenderMethod("FillARGB");fill->SetParameterColor4B(0,0x98765432);
        Operation(TVPGetRenderManager(),fill,survivor.get(),tTVPRect(0,0,2,2),nullptr,tTVPRect());
        Require(survivor->GetPoint(0,0)==0x98765432,"detached texture incompatible with software writes");
#ifdef TEST_NATIVE_METAL
        SDL_DestroyWindow(window); SDL_Quit();
        std::cout<<"PASS native Metal vs actual software RenderManager: ";
#else
        std::cout<<"PASS device-double synchronization/lifetime (native pixel checks run on macOS): ";
#endif
        std::cout<<comparisons<<" comparisons, 3 sessions\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
