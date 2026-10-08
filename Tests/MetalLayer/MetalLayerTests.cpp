#include "tjsCommHead.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "TVPCompositor.h"
#include "LayerPerspectiveGeometry.h"
#include "LayerTransitionGeometry.h"
#include "LayerShrinkGeometry.h"
#include "PointReadTrace.h"
#include "CPUConsumerTrace.h"
#include "AsyncAlphaTileCache.h"
#include "../../Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/emoteperformance.h"
#include <SDL3/SDL.h>
#include "gl/tvpgl.h"
#include "CapabilityAuditTests.h"
#ifdef TEST_NATIVE_METAL
#include "backend/MetalRenderBackend.h"
#include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include "ProductionAffineMath.inc"
#include <cstring>
#include <iostream>
#include <memory>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <thread>
#include <functional>
using krkrsdl3::iTVPRenderBackend;
using Texture=std::unique_ptr<iTVPTexture2D>;
extern bool TVPTestCaptureLogs;
extern std::vector<std::string> TVPTestLogs;
void BitmapOverwriteTests(iTVPRenderBackend* backend);
void C4BitmapResourceTests(iTVPRenderBackend*);
void TransitionOutputTests();
void BitmapRenderSessionCacheTests(iTVPRenderBackend* backend);
void UnivTransShaderTests();
void LayerBlendShaderTests();
void TriangleProfileTests();
void CompilationFailureTests();
void OperationContractTests();
void C0ProfileTests();
void P1AShaderTests();
void P1BShaderTests();
void TransitionHandlerTests(iTVPRenderBackend*);
void TransitionContractTests();
void TransitionHandlerDispatchFailureTests(const std::function<void(bool)>&);
void TransitionHandlerPipelineFailureTests(const std::function<void(bool)>&);
void C4ProfileTests();
void C1ProfileTests();
void ShrinkCopyTests(iTVPRenderBackend*);
void ShrinkCopyFailureTests(const std::function<void(int,bool)>&);
void ShrinkShaderContractTests();
void RunC2ConsumerTraceTests();
int RunC2LayerExBoundaryTests();
void TVPTestShrinkExecute(const TVPLayerShrinkOperation&,const uint8_t*,int,std::vector<uint8_t>&,int);
uint32_t TVPTestTransitionPixel(const TVPLayerTransitionOperation&,int,int,const uint8_t*,int,const uint8_t*,int);
void P1AGammaTests(iTVPRenderBackend*);
uint32_t TVPTestP1AGammaPixel(uint32_t,const TVPLayerOperation&,const uint8_t*);
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
// logic on non-Apple hosts. Rects dispatch to actual software methods; affine
// coordinates/bytes and UnivTrans use helpers extracted from production MSL.
// Full shader execution and texture ordering are checked on native Metal CI.
class DeviceDouble : public iTVPRenderBackend {
    struct Resource { std::vector<uint8_t> pixels; int w,h,bpp; };
    std::unordered_map<void*,std::unique_ptr<Resource>> resources;
    bool preserveSoftwareStretch=false;
public:
    uint64_t fullReads=0, updateCalls=0;
    bool failDiagnosticUpload=false, cycleDiagnosticsOnRead=false;
    bool cycleDiagnosticsOnUpload=false, cycleDiagnosticsOnRegionRead=false;
    bool rejectTripleSource=false;
    bool throwAfterTransitionDispatch=false;
    bool rejectTransition=false;
    int shrinkFailureStage=-1;
    bool shrinkWideAvailable=true;
    uint64_t shrinkCalls=0;
    TVPLayerShrinkResult shrinkResult=TVPLayerShrinkResult::Applied;
    bool SupportsLayerShrinks() const override { return true; }
    bool SupportsLayerShrink64() const override { return shrinkWideAvailable; }
    TVPLayerShrinkResult LastLayerShrinkResult() const override { return shrinkResult; }
    bool OperateLayerShrink(const TVPLayerShrinkOperation& op,void* target,void* source) override {
        ++shrinkCalls;shrinkResult=TVPLayerShrinkResult::BackendFailure;
        auto t=resources.find(target),s=resources.find(source);
        if(t==resources.end() || s==resources.end() || t->second->bpp!=4 || s->second->bpp!=4) {
            shrinkResult=TVPLayerShrinkResult::Resource;return false;
        }
        const auto valid=TVPLayerShrinkGeometry::Validate(op,t->second->w,t->second->h,s->second->w,s->second->h,target==source);
        if(valid!=TVPLayerShrinkResult::Applied) {shrinkResult=valid;return false;}
        if(op.avgBits==64 && !TVPLayerShrinkGeometry::CanUse32(op) && !shrinkWideAvailable) {
            shrinkResult=TVPLayerShrinkResult::Arithmetic;return false;
        }
        if(shrinkFailureStage==0) {shrinkResult=TVPLayerShrinkResult::ParameterBudget;return false;}
        std::vector<uint8_t> scratch;
        TVPTestShrinkExecute(op,s->second->pixels.data(),s->second->w*4,scratch,shrinkFailureStage);
        if(shrinkFailureStage>=1 && shrinkFailureStage<=3) {shrinkResult=TVPLayerShrinkResult::BackendFailure;return false;}
        const int w=op.destination.Width(),h=op.destination.Height();
        for(int y=0;y<h;++y) std::memcpy(t->second->pixels.data()+((op.destination.top+y)*t->second->w+op.destination.left)*4,
                                      scratch.data()+size_t(y)*w*4,size_t(w)*4);
        shrinkResult=TVPLayerShrinkResult::Applied;
        if(shrinkFailureStage==4) throw std::bad_alloc();
        const uint32_t bits=op.avgBits==32 || TVPLayerShrinkGeometry::CanUse32(op) ? 32 : 64;
        krkrsdl3::layer_work::RecordShrinkParameters(4,(size_t(w)+h)*(bits==32?28:48)+56,
            TVPLayerShrinkGeometry::TemporaryBytes(op,bits));
        return true;
    }
    TVPLayerTransitionResult transitionResult=TVPLayerTransitionResult::Applied;
    TVPLayerTransitionResult LastLayerTransitionResult() const override { return transitionResult; }
    bool rejectGamma=false;
    bool rejectAlphaTables=false;
    bool rejectAffine=false;
    int rejectPerspectiveQuad=-1;
    bool rejectPsTables=false;
    bool psTablesReady=false;
    std::array<uint8_t,196608> psTables{};
    TVPLayerParameterUploadStats parameterUploads;
    std::array<std::shared_ptr<const TVPLayerGammaLUT>,2> gammaCaches;
    TVPLayerParameterUploadStats GetLayerParameterUploadStats() const override { return parameterUploads; }
    bool SetLayerPsTables(const uint8_t* soft,const uint8_t* dodge,const uint8_t* burn) override {
        if(rejectPsTables || !soft || !dodge || !burn) return false;
        if(psTablesReady && !std::memcmp(psTables.data(),soft,65536) &&
           !std::memcmp(psTables.data()+65536,dodge,65536) && !std::memcmp(psTables.data()+131072,burn,65536)) return true;
        std::memcpy(psTables.data(),soft,65536); std::memcpy(psTables.data()+65536,dodge,65536);
        std::memcpy(psTables.data()+131072,burn,65536); psTablesReady=true;
        ++parameterUploads.psTableUploads; parameterUploads.psTableUploadedBytes+=psTables.size();
        return true;
    }
    uint64_t asyncRequests=0, syncRegionReads=0, alphaSerial=1;
    uint64_t simulatedReadWaitNS=0;
    uint64_t GetLastReadbackWaitNanoseconds() const override { return simulatedReadWaitNS; }
    std::shared_ptr<krkrsdl3::AsyncLayerPresentation> alphaPresentation=
        std::make_shared<krkrsdl3::AsyncLayerPresentation>(alphaSerial);
    std::vector<std::shared_ptr<krkrsdl3::AsyncLayerReadback>> alphaReads;
    std::shared_ptr<krkrsdl3::AsyncLayerPresentation> GetCurrentLayerPresentation() const override { return alphaPresentation; }
    void NextAlphaFrame() { alphaPresentation=std::make_shared<krkrsdl3::AsyncLayerPresentation>(++alphaSerial); }
    void CompleteAlphaReads(bool presented) {
        for(auto& read:alphaReads) read->completed.store(true);
        alphaPresentation->presented.store(presented);
        alphaReads.clear();
    }
    bool RequestLayerTextureRegionRead(void* handle,const TVPLayerRect& region,
                                      const std::shared_ptr<krkrsdl3::AsyncLayerReadback>& read) override {
        auto& texture=*resources.at(handle);
        if(texture.bpp!=4 || region.left<0 || region.top<0 || region.right>texture.w || region.bottom>texture.h) return false;
        read->region=region;
        if(!read->presentation) read->presentation=alphaPresentation;
        read->pitch=region.Width()*4; read->rgba.resize(size_t(read->pitch)*region.Height());
        for(int y=0;y<region.Height();++y) std::memcpy(read->rgba.data()+size_t(y)*read->pitch,
            texture.pixels.data()+(size_t(y+region.top)*texture.w+region.left)*4,read->pitch);
        alphaReads.push_back(read); ++asyncRequests; return true;
    }
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
    bool SupportsLayerTransitions() const override { return true; }
    bool OperateLayerTransition(const TVPLayerTransitionOperation& op,void* target,void* source1,void* source2) override {
        if(rejectTransition) {transitionResult=TVPLayerTransitionResult::PipelineUnavailable;return false;}
        auto t=resources.find(target),s1=resources.find(source1),s2=resources.find(source2);
        if(t==resources.end() || s1==resources.end() || s2==resources.end() ||
           target==source1 || target==source2 || t->second->bpp!=4 || s1->second->bpp!=4 || s2->second->bpp!=4) return false;
        auto& p=op.params;
        transitionResult=layer_transition::Validate(op,t->second->w,t->second->h,s1->second->w,s1->second->h,
             s2->second->w,s2->second->h);
        if(transitionResult!=TVPLayerTransitionResult::Applied) return false;
        for(int y=0;y<p.height;++y) for(int x=0;x<p.width;++x) {
            const auto pixel=TVPTestTransitionPixel(op,p.left+x,p.top+y,
                s1->second->pixels.data(),s1->second->w*4,s2->second->pixels.data(),s2->second->w*4);
            std::memcpy(t->second->pixels.data()+size_t((p.destTop+y)*t->second->w+p.destLeft+x)*4,&pixel,4);
        }
        if(throwAfterTransitionDispatch) throw std::bad_alloc();
        krkrsdl3::layer_work::RecordTransitionParameters(1,sizeof(op.params));
        ++parameterUploads.transitionParameterUploads;
        parameterUploads.transitionParameterUploadedBytes+=sizeof(op.params);
        return true;
    }
    bool SetLayerAlphaTables(const uint8_t*,const uint8_t*) override { return !rejectAlphaTables; }
    void* CreateLayerTexture(int w,int h,TVPLayerTextureFormat f) override {
        auto r=std::make_unique<Resource>(); r->w=w;r->h=h;r->bpp=f==TVPLayerTextureFormat::R8?1:4;
        r->pixels.resize(size_t(w)*h*r->bpp); auto* key=r.get(); resources[key]=std::move(r); return key;
    }
    void DestroyLayerTexture(void* handle) override { resources.erase(handle); }
    bool UpdateLayerTexture(void* handle,const uint8_t* data,int pitch,const TVPLayerRect& rc) override {
        if(failDiagnosticUpload) return false;
        ++updateCalls;
        if(cycleDiagnosticsOnUpload) {
            cycleDiagnosticsOnUpload=false;
            krkrsdl3::layer_work::SetEnabled(false); krkrsdl3::layer_work::SetEnabled(true);
        }
        auto& r=*resources.at(handle);
        for(int y=0;y<rc.Height();++y) std::memcpy(r.pixels.data()+((y+rc.top)*r.w+rc.left)*r.bpp,data+y*pitch,rc.Width()*r.bpp);
        return true;
    }
    bool ReadLayerTexture(void* handle,std::vector<uint8_t>& pixels,int& pitch) override {
        ++fullReads;
        if(cycleDiagnosticsOnRead) {
            cycleDiagnosticsOnRead=false;
            krkrsdl3::layer_work::SetEnabled(false); krkrsdl3::layer_work::SetEnabled(true);
        }
        simulatedReadWaitNS=9000000;
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
    bool CopyTargetToLayerTextureRegion(void* source,void* destination,const TVPLayerRect& region) override {
        auto s=resources.find(source), d=resources.find(destination);
        if(s==resources.end() || d==resources.end() || source==destination) return false;
        auto& src=*s->second; auto& dst=*d->second;
        if(src.bpp!=4 || dst.bpp!=4 || src.w!=dst.w || src.h!=dst.h ||
            region.left<0 || region.top<0 || region.right>dst.w || region.bottom>dst.h) return false;
        for(int y=region.top;y<region.bottom;++y) std::memcpy(dst.pixels.data()+(size_t(y)*dst.w+region.left)*4,
            src.pixels.data()+(size_t(y)*src.w+region.left)*4,size_t(region.Width())*4);
        return true;
    }
    bool ReadLayerTextureRegion(void* handle,const TVPLayerRect& rc,std::vector<uint8_t>& pixels,int& pitch) override {
        if(cycleDiagnosticsOnRegionRead) {
            cycleDiagnosticsOnRegionRead=false;
            krkrsdl3::layer_work::SetEnabled(false); krkrsdl3::layer_work::SetEnabled(true);
        }
        simulatedReadWaitNS=9000000;
        ++syncRegionReads;
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
    static iTVPRenderMethod* SoftwareMethod(const TVPLayerOperation& op) {
        auto* sw=TVPGetSoftwareRenderManager(); const char* name=nullptr;
        switch(op.kind) {
            case TVPLayerOperationKind::Copy: name="Copy"; break;
            case TVPLayerOperationKind::Alpha: name="AlphaBlend"; break;
            case TVPLayerOperationKind::ConstAlpha: name="ConstAlphaBlend"; break;
            case TVPLayerOperationKind::AdditiveAlpha: name="AdditiveAlphaBlend"; break;
            case TVPLayerOperationKind::PsMul: name="PsMulBlend"; break;
            case TVPLayerOperationKind::PsOverlay: name="PsOverlayBlend"; break;
            case TVPLayerOperationKind::PsHardLight: name="PsHardLightBlend"; break;
            case TVPLayerOperationKind::PsScreen: name="PsScreenBlend"; break;
            case TVPLayerOperationKind::PsColorDodge5: name="PsColorDodge5Blend"; break;
            case TVPLayerOperationKind::PsAlpha: name="PsAlphaBlend"; break;
            case TVPLayerOperationKind::PsAdd: name="PsAddBlend"; break;
            case TVPLayerOperationKind::PsSub: name="PsSubBlend"; break;
            case TVPLayerOperationKind::PsSoftLight: name="PsSoftLightBlend"; break;
            case TVPLayerOperationKind::PsColorDodge: name="PsColorDodgeBlend"; break;
            case TVPLayerOperationKind::PsColorBurn: name="PsColorBurnBlend"; break;
            case TVPLayerOperationKind::PsLighten: name="PsLightenBlend"; break;
            case TVPLayerOperationKind::PsDarken: name="PsDarkenBlend"; break;
            case TVPLayerOperationKind::PsDiff: name="PsDiffBlend"; break;
            case TVPLayerOperationKind::PsDiff5: name="PsDiff5Blend"; break;
            case TVPLayerOperationKind::PsExclusion: name="PsExclusionBlend"; break;
            default: return nullptr;
        }
        std::string methodName=name;
        if(op.flags&TVP_LAYER_DEST_ALPHA) methodName+="_d";
        else if(op.flags&TVP_LAYER_DEST_PREMULTIPLIED) methodName+="_a";
        else if(op.kind==TVPLayerOperationKind::ConstAlpha && (op.flags&TVP_LAYER_HOLD_ALPHA)) methodName+="_HDA";
        auto* method=sw->GetRenderMethod(methodName.c_str());
        method->SetParameterOpa(method->EnumParameterID("opacity"),op.opacity);
        return method;
    }
    bool OperateLayerRect(const TVPLayerOperation& op,void* target,const TVPLayerRect& dst,void* source,const TVPLayerRect& src,int sampling) override {
        const auto* traits=TVPGetLayerOperationTraits(op.kind);
        if(!traits || traits->backendInputCount>1) return false;
        if((traits->parameterResources&TVP_LAYER_RESOURCE_PS_TABLES) && !psTablesReady) return false;
        if(TVPLayerOperationRequiresForwardSource(op.kind) && (src.Width()<=0 || src.Height()<=0)) return false;
        if(op.kind==TVPLayerOperationKind::AdjustGamma) {
            if(rejectGamma || !op.gammaLUT || !target) return false;
            auto& t=*resources.at(target);
            if(t.bpp!=4 || dst.Width()<=0 || dst.Height()<=0) return false;
            auto& previous=gammaCaches[(op.flags&TVP_LAYER_DEST_PREMULTIPLIED) ? 1 : 0];
            if(!previous || previous->version!=op.gammaLUT->version || previous->bytes!=op.gammaLUT->bytes) {
                previous=op.gammaLUT;
                ++parameterUploads.gammaLUTUploads;
                parameterUploads.gammaLUTUploadedBytes+=op.gammaLUT->bytes.size();
            }
            for(int y=std::max(0,dst.top);y<std::min(t.h,dst.bottom);++y)
            for(int x=std::max(0,dst.left);x<std::min(t.w,dst.right);++x) {
                auto* address=t.pixels.data()+(size_t(y)*t.w+x)*4;
                uint32_t before; std::memcpy(&before,address,4);
                uint32_t after=TVPTestP1AGammaPixel(before,op,op.gammaLUT->bytes.data());
                std::memcpy(address,&after,4);
            }
            return true;
        }
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
            case TVPLayerOperationKind::AdditiveAlpha: name="AdditiveAlphaBlend"; break;
            case TVPLayerOperationKind::PsMul: name="PsMulBlend"; break;
            case TVPLayerOperationKind::PsOverlay: name="PsOverlayBlend"; break;
            case TVPLayerOperationKind::PsHardLight: name="PsHardLightBlend"; break;
            case TVPLayerOperationKind::AlphaToAdditiveAlpha: name="AlphaToAdditiveAlpha"; break;
            case TVPLayerOperationKind::GrayScale: name="DoGrayScale"; break;
            case TVPLayerOperationKind::CopyBlueToAlpha: name="CopyBlueToAlpha"; break;
            case TVPLayerOperationKind::MultiplyAlpha: name="MultiplyAlpha"; break;
            case TVPLayerOperationKind::BoxBlur: name="BoxBlur"; break;
            case TVPLayerOperationKind::PsScreen: name="PsScreenBlend"; break;
            case TVPLayerOperationKind::PsColorDodge5: name="PsColorDodge5Blend"; break;
            case TVPLayerOperationKind::Add: name="AddBlend"; break;
            case TVPLayerOperationKind::Sub: name="SubBlend"; break;
            case TVPLayerOperationKind::Mul: name="MulBlend"; break;
            case TVPLayerOperationKind::ColorDodge: name="ColorDodgeBlend"; break;
            case TVPLayerOperationKind::Darken: name="DarkenBlend"; break;
            case TVPLayerOperationKind::Lighten: name="LightenBlend"; break;
            case TVPLayerOperationKind::Screen: name="ScreenBlend"; break;
            case TVPLayerOperationKind::RemoveOpacity: name="RemoveOpacity"; break;
            case TVPLayerOperationKind::AdditiveAlphaToAlpha: name="AdditiveAlphaToAlpha"; break;
            case TVPLayerOperationKind::AlphaSD: name="AlphaBlend_SD"; break;
            case TVPLayerOperationKind::PsAlpha: name="PsAlphaBlend"; break;
            case TVPLayerOperationKind::PsAdd: name="PsAddBlend"; break;
            case TVPLayerOperationKind::PsSub: name="PsSubBlend"; break;
            case TVPLayerOperationKind::PsSoftLight: name="PsSoftLightBlend"; break;
            case TVPLayerOperationKind::PsColorDodge: name="PsColorDodgeBlend"; break;
            case TVPLayerOperationKind::PsColorBurn: name="PsColorBurnBlend"; break;
            case TVPLayerOperationKind::PsLighten: name="PsLightenBlend"; break;
            case TVPLayerOperationKind::PsDarken: name="PsDarkenBlend"; break;
            case TVPLayerOperationKind::PsDiff: name="PsDiffBlend"; break;
            case TVPLayerOperationKind::PsDiff5: name="PsDiff5Blend"; break;
            case TVPLayerOperationKind::PsExclusion: name="PsExclusionBlend"; break;
            default:
                name=op.kind==TVPLayerOperationKind::Alpha ? "AlphaBlend" : op.kind==TVPLayerOperationKind::ConstAlpha ? "ConstAlphaBlend" : "ApplyColorMap";
                break;
        }
        std::string methodName=name;
        if(op.kind>=TVPLayerOperationKind::Alpha) {
            if(op.flags&TVP_LAYER_DEST_ALPHA) methodName+="_d";
            else if(op.flags&TVP_LAYER_DEST_PREMULTIPLIED) methodName+="_a";
            else if((op.kind==TVPLayerOperationKind::ConstAlpha || op.kind==TVPLayerOperationKind::Mul) &&
                    (op.flags&TVP_LAYER_HOLD_ALPHA)) methodName+="_HDA";
        }
        auto* method=sw->GetRenderMethod(methodName.c_str());
        method->SetParameterOpa(method->EnumParameterID("opacity"),op.opacity);
        method->SetParameterColor4B(method->EnumParameterID("color"),op.color);
        if(op.kind==TVPLayerOperationKind::BoxBlur) {
            method->SetParameterInt(method->EnumParameterID("area_left"),0);
            method->SetParameterInt(method->EnumParameterID("area_top"),0);
            method->SetParameterInt(method->EnumParameterID("area_right"),op.phase-1);
            method->SetParameterInt(method->EnumParameterID("area_bottom"),op.vague-1);
        }
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
        if(!preserveSoftwareStretch) sw->SetParameterInt(sw->EnumParameterID("StretchType"),sampling);
        sw->OperateRect(method,tv.get(),nullptr,Rect(dst),tRenderTexRectArray(source?&input:nullptr,source?1:0));return true;
    }
    bool OperateLayerAffine(const TVPLayerOperation& op,void* target,const TVPLayerAffineCopy& map,void* source,int sampling) override {
        if(!TVPLayerOperationSupportsAffine(op) || !target || !source || sampling<0 || sampling>1) return false;
        if(rejectAffine) return false;
        const auto* traits=TVPGetLayerOperationTraits(op.kind);
        if((traits->parameterResources&TVP_LAYER_RESOURCE_PS_TABLES) && !psTablesReady) return false;
        auto& t=*resources.at(target); auto& s=*resources.at(source);
        // Snapshot independent of destination: the production backend blits on alias.
        const auto pixels=s.pixels;
        const auto& rc=map.sourceCrop; const auto& clip=map.clip;
        if(t.bpp!=4 || s.bpp!=4 || rc.left<0 || rc.top<0 || rc.right>s.w || rc.bottom>s.h ||
           rc.Width()<=0 || rc.Height()<=0 || clip.left<0 || clip.top<0 || clip.right>t.w || clip.bottom>t.h ||
           clip.Width()<=0 || clip.Height()<=0) return false;
        for(double value:map.inverse) if(!std::isfinite(value) || std::abs(value)>1000000) return false;
        if(op.kind!=TVPLayerOperationKind::Copy && map.inverse[0]*map.inverse[4]-map.inverse[1]*map.inverse[3]==0) return false;
        std::vector<uint8_t> sampled(size_t(clip.Width())*clip.Height()*4);
        auto coordinate=[&](int row,int x,int y) {
            float h[3],l[3];
            for(int i=0;i<3;++i) { h[i]=float(map.inverse[row*3+i]); l[i]=float(map.inverse[row*3+i]-double(h[i])); }
            return affine_shader::affineCoordinate(h[0],l[0],h[1],l[1],h[2],l[2],float(x)+0.5f,float(y)+0.5f);
        };
        const int w=rc.Width(),h=rc.Height();
        for(int y=clip.top;y<clip.bottom;++y) for(int x=clip.left;x<clip.right;++x) {
            float sx=coordinate(0,x-clip.left,y-clip.top),sy=coordinate(1,x-clip.left,y-clip.top);
            uint8_t* out=sampled.data()+((y-clip.top)*clip.Width()+x-clip.left)*4;
            if(sx<0.5f || sx>=w-0.5f || sy<0.5f || sy>=h-0.5f) continue;
            auto pixel=[&](int px,int py,int c) {return int(pixels[((py+rc.top)*s.w+px+rc.left)*4+c]);};
            if(sampling==0) {
                for(int c=0;c<4;++c) out[c]=pixel(std::clamp(int(sx+0.5f),0,w-1),std::clamp(int(sy+0.5f),0,h-1),c);
            } else {
                int ax=std::clamp(int(sx),0,std::max(0,w-2)),ay=std::clamp(int(sy),0,std::max(0,h-2));
                int bx=std::min(ax+1,w-1),by=std::min(ay+1,h-1);
                float fx=w==1?0:sx-ax,fy=h==1?0:sy-ay;
                for(int c=0;c<4;++c) out[c]=affine_shader::affineBilinearByte(fx,fy,pixel(ax,ay,c),pixel(bx,ay,c),pixel(ax,by,c),pixel(bx,by,c));
            }
        }
        // Sampling is extracted production MSL. Blending here deliberately uses
        // initialized software methods: this double proves routing/residency and
        // sampled-frame semantics, not native MSL pixel execution. Equal extents
        // avoid touching global StretchType or resampling the prepared frame.
        if(op.kind==TVPLayerOperationKind::Copy) {
            for(int y=clip.top;y<clip.bottom;++y) std::memcpy(t.pixels.data()+(size_t(y)*t.w+clip.left)*4,
                sampled.data()+size_t(y-clip.top)*clip.Width()*4,size_t(clip.Width())*4);
        } else {
            auto* sw=TVPGetSoftwareRenderManager();
            auto* method=SoftwareMethod(op); if(!method) return false;
            Texture tv(sw->CreateTexture2D(t.pixels.data(),t.w*4,t.w,t.h,TVPTextureFormat::RGBA));
            Texture sv(sw->CreateTexture2D(sampled.data(),clip.Width()*4,clip.Width(),clip.Height(),TVPTextureFormat::RGBA));
            std::pair<iTVPTexture2D*,tTVPRect> input{sv.get(),tTVPRect(0,0,clip.Width(),clip.Height())};
            sw->OperateRect(method,tv.get(),nullptr,Rect(clip),tRenderTexRectArray(&input,1));
        }
        return true;
    }
    bool OperateLayerPerspective(const TVPLayerOperation& op,void* target,const TVPLayerPerspectiveQuad* quads,
                                size_t count,void* source,int sampling) override {
        if(!TVPLayerOperationSupportsPerspective(op) || sampling<0 || sampling>1 || count>TVP_LAYER_PERSPECTIVE_MAX_QUADS) return false;
        if(!count) return true;
        if(!quads || !resources.count(target) || !resources.count(source)) return false;
        auto& t=*resources.at(target);auto& s=*resources.at(source);
        if(t.bpp!=4 || s.bpp!=4) return false;
        const auto* traits=TVPGetLayerOperationTraits(op.kind);
        if((traits->parameterResources&TVP_LAYER_RESOURCE_PS_TABLES) && !psTablesReady) return false;
        auto valid=[](const TVPLayerRect& r,int w,int h){return r.left>=0&&r.top>=0&&r.right>=r.left&&r.bottom>=r.top&&r.right<=w&&r.bottom<=h;};
        for(size_t i=0;i<count;++i) {
            const auto& q=quads[i];if(!valid(q.clip,t.w,t.h)) return false;
            if(!q.clip.Width() || !q.clip.Height()) continue;
            if(q.rectangle) {
                if(!valid(q.source,s.w,s.h)||q.source.Width()<=0||q.source.Height()<=0||!valid(q.destination,t.w,t.h) ||
                   q.destination.Width()<=0||q.destination.Height()<=0 ||
                   q.clip.left<q.destination.left||q.clip.top<q.destination.top||q.clip.right>q.destination.right||q.clip.bottom>q.destination.bottom) return false;
                if(target==source && (q.source.left!=q.destination.left||q.source.top!=q.destination.top||q.source.right!=q.destination.right||q.source.bottom!=q.destination.bottom)) return false;
            } else {
                if(!layer_perspective::ValidateInverse(q.inverse)) return false;
                for(double v:q.inverse) if(std::abs(v)>1000000) return false;
            }
        }
        // The caller already selected the software filter. Rectangular quads
        // use that equivalent 0/1/2 filter, and prepared frames have equal
        // extents, so neither needs to change it. A late transactional failure
        // must leave the original filter intact for the facade's CPU fallback.
        struct RestoreStretchPolicy { bool& flag;bool saved;~RestoreStretchPolicy(){flag=saved;} } restore{preserveSoftwareStretch,preserveSoftwareStretch};
        preserveSoftwareStretch=true;
        void* work=CreateLayerTexture(t.w,t.h,TVPLayerTextureFormat::RGBA8);
        void* snapshot=CreateLayerTexture(s.w,s.h,TVPLayerTextureFormat::RGBA8);
        auto release=[&](void*){DestroyLayerTexture(work);DestroyLayerTexture(snapshot);};
        std::unique_ptr<void,decltype(release)> cleanup(work,release);
        resources.at(work)->pixels=t.pixels;
        for(size_t i=0;i<count;++i) {
            if(rejectPerspectiveQuad==int(i)) return false; // Transactional failure after a written temporary prefix.
            const auto& q=quads[i];const auto& clip=q.clip;
            if(!clip.Width()||!clip.Height()) continue;
            resources.at(snapshot)->pixels=target==source?resources.at(work)->pixels:s.pixels;
            if(q.rectangle) {
                if(!OperateLayerRect(op,work,q.destination,snapshot,q.source,sampling)) return false;
                continue;
            }
            const auto& pixels=resources.at(snapshot)->pixels;
            float hi[9],lo[9];for(int k=0;k<9;++k){hi[k]=float(q.inverse[k]);lo[k]=float(q.inverse[k]-double(hi[k]));}
            auto coordinate=[&](int row,int x,int y){int j=row*3;return affine_shader::perspectiveCoordinate(hi[j],lo[j],hi[j+1],lo[j+1],hi[j+2],lo[j+2],hi[6],lo[6],hi[7],lo[7],hi[8],lo[8],float(x)+0.5f,float(y)+0.5f);};
            std::vector<uint8_t> frame(size_t(clip.Width())*clip.Height()*4);
            auto pixel=[&](int x,int y,int c){return int(pixels[(size_t(y)*s.w+x)*4+c]);};
            for(int y=0;y<clip.Height();++y) for(int x=0;x<clip.Width();++x) {
                float sx=coordinate(0,x,y),sy=coordinate(1,x,y);
                if(sx<0.5f||sx>=s.w-0.5f||sy<0.5f||sy>=s.h-0.5f||!std::isfinite(sx)||!std::isfinite(sy)) continue;
                auto* out=frame.data()+(size_t(y)*clip.Width()+x)*4;
                if(!sampling) {for(int c=0;c<4;++c) out[c]=pixel(std::clamp(int(sx+0.5f),0,s.w-1),std::clamp(int(sy+0.5f),0,s.h-1),c);}
                else {
                    int ax=std::clamp(int(sx),0,std::max(0,s.w-2)),ay=std::clamp(int(sy),0,std::max(0,s.h-2));
                    int bx=std::min(ax+1,s.w-1),by=std::min(ay+1,s.h-1);float fx=s.w==1?0:sx-ax,fy=s.h==1?0:sy-ay;
                    for(int c=0;c<4;++c) out[c]=affine_shader::affineBilinearByte(fx,fy,pixel(ax,ay,c),pixel(bx,ay,c),pixel(ax,by,c),pixel(bx,by,c));
                }
            }
            void* sampled=CreateLayerTexture(clip.Width(),clip.Height(),TVPLayerTextureFormat::RGBA8);
            resources.at(sampled)->pixels=std::move(frame);
            bool ok=OperateLayerRect(op,work,clip,sampled,TVPLayerRect{0,0,clip.Width(),clip.Height()},0);
            DestroyLayerTexture(sampled);if(!ok) return false;
        }
        t.pixels=resources.at(work)->pixels;return true;
    }
    bool OperateLayerRectDualSource(const TVPLayerOperation& op,void* target,const TVPLayerRect& dst,
                                    void* source1,const TVPLayerRect& src1,
                                    void* source2,const TVPLayerRect& src2) override {
        if(op.kind!=TVPLayerOperationKind::ConstAlphaSD || !target || !source1 || !source2) return false;
        auto* sw=TVPGetSoftwareRenderManager();
        const char* name=(op.flags&TVP_LAYER_DEST_ALPHA) ? "ConstAlphaBlend_SD_d" :
            (op.flags&TVP_LAYER_DEST_PREMULTIPLIED) ? "ConstAlphaBlend_SD_a" : "ConstAlphaBlend_SD";
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
#ifndef TEST_NATIVE_METAL
static void AsyncLayerAlpha(DeviceDouble& backend) {
    SDL_SetHint("MIKAGE_EMOTE_ASYNC_ALPHA","1");
    backend.NextAlphaFrame();
    auto pixels=Image(64,64,4,71);
    auto texture=Create(TVPGetRenderManager(),64,64,TVPTextureFormat::RGBA,pixels);
    std::shared_ptr<krkrsdl3::AsyncAlphaTile> tile;
    Require(TVPRequestEmoteAsyncAlpha(texture.get(),37,45,tile) && tile,"production Layer refused eligible alpha demand");
    const auto requests=backend.asyncRequests, waits=backend.syncRegionReads;
    texture->GetTextureHandle(); texture->GetTextureHandleForRegionWrite();
    TVPEncodeEmoteAsyncAlphaForPresentation(texture.get());
    Require(backend.asyncRequests==requests,"ordinary handle/region-write query published a displayed alpha frame");
    TVPBeginEmoteAlphaPresentation();
    TVPEncodeEmoteAsyncAlphaForPresentation(texture.get());
    TVPEncodeEmoteAsyncAlphaForPresentation(texture.get());
    TVPEndEmoteAlphaPresentation();
    Require(backend.asyncRequests==requests+1 && backend.syncRegionReads==waits,
            "window alpha snapshot duplicated requests or used synchronous reads");
    backend.CompleteAlphaReads(false);
    Require(!tile->Latest(),"production Layer published GPU-completed but unpresented alpha");
    backend.alphaPresentation->presented.store(true);
    uint8_t alpha=0; std::shared_ptr<krkrsdl3::AsyncAlphaTileSnapshot> sample;
    Require(tile->Sample(37,45,alpha,sample) && alpha==pixels[(45*64+37)*4+3],"production Layer alpha tile changed pixels");
    uint64_t identity=0,version=0,afterID=0,afterVersion=0;
    Require(texture->GetContentKey(identity,version),"production Layer lacks content identity");
    texture->GetPointAlpha(1,1); const auto cachedReads=backend.syncRegionReads;
    texture->CommitGPURegionWrite(tTVPRect(32,32,64,64));
    texture->GetContentKey(afterID,afterVersion);
    Require(afterID==identity && afterVersion>version,"region transaction did not advance content version");
    texture->GetPointAlpha(1,1);
    Require(backend.syncRegionReads==cachedReads,"region write invalidated unrelated exact alpha cache");
    // Script pixel reads still use the current texture, independently of the
    // UI snapshot and even after a presented old version is held by an event.
    texture->GetPointAlpha(37,45);
    Require(backend.syncRegionReads==cachedReads+1,"explicit script pixel query borrowed stale UI alpha");
    TVPStopEmoteAsyncAlphaDemand(texture.get()); backend.NextAlphaFrame();
    TVPBeginEmoteAlphaPresentation(); TVPEncodeEmoteAsyncAlphaForPresentation(texture.get()); TVPEndEmoteAlphaPresentation();
    Require(backend.asyncRequests==requests+1,"idle Layer continued GPU alpha prefetch");
    // A missing tile can be encoded later from an intrusive-ref-held source
    // epoch, keeping F's display ticket even though the current frame is F+1.
    auto frozenPixels=Image(64,64,4,93);
    auto frozen=Create(TVPGetRenderManager(),64,64,TVPTextureFormat::RGBA,frozenPixels);
    std::shared_ptr<krkrsdl3::AsyncAlphaTile> frozenTile;
    Require(TVPRequestEmoteAsyncAlpha(frozen.get(),11,12,frozenTile),"frozen alpha demand failed");
    auto chosen=backend.alphaPresentation; chosen->presented.store(true);
    frozen->GetTextureHandle(); frozen->AddRef();
    std::shared_ptr<iTVPTexture2D> epoch(frozen.get(),[](iTVPTexture2D* t){t->Release();});
    backend.NextAlphaFrame();
    TVPEncodeFrozenEmoteAsyncAlpha(epoch.get(),chosen);
    backend.CompleteAlphaReads(false);
    auto frozenSample=frozenTile->AtFrame(chosen->frameSerial);
    Require(frozenSample && frozenSample->alpha[size_t(12)*32+11]==frozenPixels[(12*64+11)*4+3],
            "supplemented production Layer read changed alpha or used the later frame ticket");
    SDL_SetHint("MIKAGE_EMOTE_ASYNC_ALPHA","0");
}
#endif
static void Equivalence() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    const char* methods[]={"Copy","CopyColor","CopyMask","CopyOpaqueImage","FillARGB","FillColor","FillMask",
        "AlphaBlend","AlphaBlend_HDA","AlphaBlend_d","AlphaBlend_a","ConstAlphaBlend","ConstAlphaBlend_HDA","ConstAlphaBlend_d","ConstAlphaBlend_a",
        "ApplyColorMap","ApplyColorMap_d","ApplyColorMap_a","ConstColorAlphaBlend","ConstColorAlphaBlend_d","ConstColorAlphaBlend_a",
        "RemoveConstOpacity","AdditiveAlphaBlend","AdditiveAlphaBlend_HDA","AdditiveAlphaBlend_a",
        "PsMulBlend","PsMulBlend_HDA","PsOverlayBlend","PsOverlayBlend_HDA","PsHardLightBlend","PsHardLightBlend_HDA"};
    for(auto* name:methods) for(int opacity:{0,1,63,127,128,254,255}) {
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
    auto* gray=gpu->GetRenderMethod("PsAlphaBlend");
    auto* sw=TVPGetSoftwareRenderManager(); auto expected=Create(sw,9,7,TVPTextureFormat::RGBA,image);
    auto actual=Create(gpu,9,7,TVPTextureFormat::RGBA,image);
    actual->GetPersistentCPUData(true);
    auto beforeFallback=TVPGetMetalLayerRenderStats().cpuFallbacks;
    Operation(sw,gray,expected.get(),tTVPRect(0,0,9,7),expected.get(),tTVPRect(0,0,9,7));
    Operation(gpu,gray,actual.get(),tTVPRect(0,0,9,7),actual.get(),tTVPRect(0,0,9,7));
    Require(TVPGetMetalLayerRenderStats().cpuFallbacks==beforeFallback+1,"unsupported operator did not fall back");
    Compare(expected.get(),actual.get(),0,"software fallback");
    actual->ReleasePersistentCPUData(nullptr);
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
static void UIPointWaitCounters() {
#ifndef TEST_NATIVE_METAL
    namespace trace=krkrsdl3::point_trace;
    struct Restore { bool enabled=trace::Enabled(); ~Restore() { trace::SetEnabled(enabled); } } restore;
    trace::SetEnabled(true);
    auto* gpu=TVPGetRenderManager(); auto image=Image(16,12,4,3);
    auto texture=Create(gpu,16,12,TVPTextureFormat::RGBA,image);
    const auto before=emoteplayer::performanceStats();
    {
        trace::TriggerScope trigger(trace::Trigger::PointerDown,false);
        trace::OriginScope source(trace::Source::LayerHitTest,texture.get());
        texture->GetPointAlpha(0,0); texture->GetPointAlpha(0,0);
    }
    const auto after=emoteplayer::performanceStats();
    Require(after.uiSyncReads==before.uiSyncReads+1 && after.uiSyncWaitNS==before.uiSyncWaitNS+9000000,
            "UI counter measures one simulated GPU wait, excluding point cache hits");
    {
        trace::TriggerScope trigger(trace::Trigger::ScriptHitTest,false);
        trace::OriginScope source(trace::Source::LayerHitTest,texture.get());
        texture->GetPointAlpha(1,0);
    }
    {
        trace::TriggerScope trigger(trace::Trigger::PointerDown,false);
        trace::OriginScope source(trace::Source::LayerMask,texture.get());
        texture->GetPointAlpha(2,0);
    }
    const auto script=emoteplayer::performanceStats();
    Require(script.uiSyncReads==after.uiSyncReads && script.uiSyncWaitNS==after.uiSyncWaitNS,
            "explicit script hit/pixel queries are not billed to built-in UI waits");
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

static void ScopedNativePixels() {
    auto* gpu=TVPGetRenderManager(); auto image=Image(32,24,4,18);
    auto texture=Create(gpu,32,24,TVPTextureFormat::RGBA,image);
    const auto initial=TVPGetMetalLayerRenderStats();
    {
        tTVPScopedTexturePixels read; read.Acquire(texture.get(),false,"test.native.read");
        Require(texture->IsIndependent(),"temporary read invented image sharing");
        Require(!texture->IsCPUResident(),"native read permanently pinned texture");
        Require(*static_cast<uint32_t*>(read.Data())==*reinterpret_cast<uint32_t*>(image.data()),"native read changed pixels");
    }
    {
        tTVPScopedTexturePixels write; write.Acquire(texture.get(),true,"test.native.write");
        Require(texture->IsCPUResident(),"active native writer allowed GPU mutation");
        SDL_SetHint("MIKAGE_EMOTE_ASYNC_ALPHA","1");
        Require(!TVPIsEmoteAsyncAlphaTexture(texture.get()),"async alpha sampled a live native writer");
        Require(!texture->GetTextureHandleForOverwrite(),"GPU overwrite raced native writer");
        static_cast<uint32_t*>(write.Data())[3*32+4]=0x70123456;
        write.Written(tTVPRect(4,3,5,4));
    }
    Require(!texture->IsCPUResident(),"completed native write kept CPU authority");
    Require(TVPIsEmoteAsyncAlphaTexture(texture.get()),"completed native write disabled future async alpha");
    SDL_SetHint("MIKAGE_EMOTE_ASYNC_ALPHA","0");
    auto before=TVPGetMetalLayerRenderStats(); texture->GetTextureHandle();
    auto after=TVPGetMetalLayerRenderStats();
    Require(after.uploadedBytes-before.uploadedBytes==4,"native ROI write uploaded entire texture");
    texture->GetTextureHandle(); texture->GetTextureHandle();
    Require(TVPGetMetalLayerRenderStats().uploadedBytes==after.uploadedBytes,"completed native write re-uploaded unchanged pixels");
    Require(after.pinnedCPUTextures==initial.pinnedCPUTextures,"native access permanently pinned a texture");
    texture->InvalidateCPUCache(); Require(texture->GetPoint(4,3)==0x70123456,"native upload lost pixels");
    // Closing native access must leave a pre-existing raw script lease alive.
    auto* raw=static_cast<uint32_t*>(texture->GetPersistentCPUData(true));
    {
        tTVPScopedTexturePixels write; write.Acquire(texture.get(),true);
        static_cast<uint32_t*>(write.Data())[0]=0x11223344; write.Written(tTVPRect(0,0,1,1));
    }
    texture->GetTextureHandle(); before=TVPGetMetalLayerRenderStats();
    raw[1]=0x8899aabb; texture->GetTextureHandle();
    Require(TVPGetMetalLayerRenderStats().uploadedBytes-before.uploadedBytes==32*24*4,"native write revoked raw script lease");
    Require(texture->GetPoint(1,0)==0x8899aabb,"raw script pointer no longer synchronized");
    // Exception unwind commits the original texture even after owner replacement.
    auto old=Create(gpu,32,24,TVPTextureFormat::RGBA,image);
    auto* retained=old.release(); retained->AddRef();
    try {
        tTVPScopedTexturePixels write; write.Acquire(retained,true);
        static_cast<uint32_t*>(write.Data())[2]=0x99887766;
        write.Written(tTVPRect(2,0,3,1)); retained->Release();
        throw std::runtime_error("test unwind");
    } catch(const std::runtime_error&) {}
    retained->GetTextureHandle(); retained->InvalidateCPUCache();
    Require(retained->GetPoint(2,0)==0x99887766,"unwind lost original texture write");
    retained->Release(); iTVPTexture2D::RecycleProcess();
}

static void WorkDiagnostics() {
    namespace work=krkrsdl3::layer_work;
    work::SetEnabled(true);
    auto image=Image(8,8,4,5); auto* gpu=TVPGetRenderManager();
    {
        work::SourceScope source("test.upload");
        auto t=Create(gpu,8,8,TVPTextureFormat::RGBA,image);
        {
            tTVPScopedTexturePixels write; write.Acquire(t.get(),true,"test.native");
            static_cast<uint32_t*>(write.Data())[0]=0x12345678;
            write.Written(tTVPRect(0,0,1,1));
        }
        t->GetTextureHandle(); t->GetTextureHandle();
    }
    auto first=work::Take();
    Require(first.transfers.find("test.upload")!=std::string::npos && first.transfers.find("test.native")!=std::string::npos,
            "production transfer lost caller attribution");
    Require(first.transfers.find("lease=1")==std::string::npos,"native write left a lease outstanding");
    Require(work::Take().transfers.empty(),"diagnostic interval did not reset");
    {
        work::StageScope outer(work::Stage::Script), inner(work::Stage::Script);
        work::StageScope load(work::Stage::ResourceLoad);
        work::RecordAMVFrame(1234);
    }
    auto stages=work::Take();
    Require(stages.stages.find("script:1/")!=std::string::npos,"nested stage scopes double counted calls");
    Require(stages.decodedFrames==1 && stages.decodedBytes==1234,"AMV decoded payload not recorded");
    std::thread worker([] { work::StageScope load(work::Stage::ResourceLoad); }); worker.join();
    Require(work::Take().stages.find("resourceLoad:0/")!=std::string::npos,"worker time was billed to main-thread stages");
    for(unsigned i=0;i<100;++i) work::Record(i%2,i,2,3,24,5,2);
    auto bounded=work::Take();
    uint64_t calls=0, bytes=0;
    size_t pos=0;
    while((pos=bounded.transfers.find('=',pos))!=std::string::npos) {
        // Skip the lease flag inside the dimensions, then read the record tuple.
        if(pos>0 && bounded.transfers.substr(pos-5,5)=="lease") { ++pos; continue; }
        unsigned long long c,b; if(std::sscanf(bounded.transfers.c_str()+pos+1,"%llu/%llu",&c,&b)==2) { calls+=c; bytes+=b; }
        ++pos;
    }
    Require(calls==100 && bytes==2400,"bounded diagnostic summary dropped overflow bytes");
    {
        work::StageScope old(work::Stage::GC);
        work::SetEnabled(false); work::SetEnabled(true);
    }
    Require(work::Take().stages.find("gc:0/")!=std::string::npos,"recording toggle retained an old stage");
    work::SetEnabled(false); work::Record(true,1,1,1,4); work::RecordAMVFrame(4);
    auto disabled=work::Take();
    Require(disabled.transfers.empty() && !disabled.decodedFrames,"disabled recording retained work diagnostics");
}

#include "C0ProductionDiagnostics.inc"
#include "C2ProductionDiagnostics.inc"
static void AlphaConversionReference() {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    auto image=Image(17,13,4,33),destination=Image(17,13,4,9);
    for(auto name:{"AlphaToAdditiveAlpha","DoGrayScale","AdditiveAlphaToAlpha"}) {
    auto* method=gpu->GetRenderMethod(name);
    for(bool alias:{false,true}) {
        auto gs=Create(gpu,17,13,TVPTextureFormat::RGBA,image),ss=Create(sw,17,13,TVPTextureFormat::RGBA,image);
        auto gd=Create(gpu,17,13,TVPTextureFormat::RGBA,destination),sd=Create(sw,17,13,TVPTextureFormat::RGBA,destination);
        auto* gt=alias ? gs.get() : gd.get(); auto* st=alias ? ss.get() : sd.get();
        tTVPRect rect(2,1,15,12); auto before=TVPGetMetalLayerRenderStats();
        sw->OperateRect(method,st,ss.get(),rect,tRenderTexRectArray());
        gpu->OperateRect(method,gt,gs.get(),rect,tRenderTexRectArray());
        auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks,"alpha conversion fell back");
        Require(after.readbackBytes==before.readbackBytes,"alpha conversion read back pixels");
        Compare(st,gt,0,"alpha conversion reference/COW");
    }
    }
}

static void P1ARectangles(iTVPRenderBackend* backend) {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    const char* names[]={"SubBlend","MulBlend","MulBlend_HDA","ColorDodgeBlend","DarkenBlend",
                        "LightenBlend","ScreenBlend","RemoveOpacity","AdditiveAlphaToAlpha","AlphaBlend_SD"};
    for(auto* name:names) for(int opacity:{0,1,127,128,254,255}) {
        auto* method=gpu->GetRenderMethod(name);
        const bool mask=!std::strcmp(name,"RemoveOpacity");
        auto source=Image(17,13,mask?1:4,91),destination=Image(17,13,4,67);
        auto format=mask?TVPTextureFormat::Gray:TVPTextureFormat::RGBA;
        auto ss=Create(sw,17,13,format,source),gs=Create(gpu,17,13,format,source);
        auto sd=Create(sw,17,13,TVPTextureFormat::RGBA,destination),gd=Create(gpu,17,13,TVPTextureFormat::RGBA,destination);
        method->SetParameterOpa(method->EnumParameterID("opacity"),opacity);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
        sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
        gs->GetTextureHandle(); gd->GetTextureHandle();
        const tTVPRect dr(3,2,14,11),sr(1,1,12,10);
        Operation(sw,method,sd.get(),dr,ss.get(),sr);
        const auto before=TVPGetMetalLayerRenderStats();
        Operation(gpu,method,gd.get(),dr,gs.get(),sr);
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "resident P1A rectangle fell back/transferred pixels");
        Compare(sd.get(),gd.get(),0,name);
    }
    // Sampling/clip routing is checked with a uniform source so its rounding
    // cannot conceal an integer blend mismatch. No new tolerance is introduced.
    for(auto* name:{"SubBlend","MulBlend","MulBlend_HDA","ColorDodgeBlend","DarkenBlend","LightenBlend","ScreenBlend","AlphaBlend_SD"}) {
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,191);
        for(int sampling:{0,1}) {
            std::vector<uint8_t> image(7*5*4); const uint32_t value=0x8b7fa9e3;
            for(size_t i=0;i<image.size();i+=4) std::memcpy(image.data()+i,&value,4);
            auto destination=Image(17,13,4,63);
            auto ss=Create(sw,7,5,TVPTextureFormat::RGBA,image),gs=Create(gpu,7,5,TVPTextureFormat::RGBA,image);
            auto sd=Create(sw,17,13,TVPTextureFormat::RGBA,destination),gd=Create(gpu,17,13,TVPTextureFormat::RGBA,destination);
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
            sw->SetParameterInt(sw->EnumParameterID("StretchType"),sampling);
            gs->GetTextureHandle(); gd->GetTextureHandle();
            const tTVPRect dr(-2,-1,15,12),sr(0,0,7,5);
            Operation(sw,method,sd.get(),dr,ss.get(),sr);
            const auto before=TVPGetMetalLayerRenderStats();
            Operation(gpu,method,gd.get(),dr,gs.get(),sr);
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.cpuFallbacks==before.cpuFallbacks && after.readbackBytes==before.readbackBytes &&
                    after.uploadedBytes==before.uploadedBytes,"P1A clip/scale left GPU residency");
            Compare(sd.get(),gd.get(),0,"P1A exact clip/scale routing");
        }
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
        sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
        for(bool shifted:{false,true}) {
            auto image=Image(17,13,4,58);
            auto sd=Create(sw,17,13,TVPTextureFormat::RGBA,image),gd=Create(gpu,17,13,TVPTextureFormat::RGBA,image);
            const tTVPRect dr(2,2,13,11),sr=shifted?tTVPRect(1,1,12,10):dr;
            Operation(sw,method,sd.get(),dr,sd.get(),sr);
            const auto before=TVPGetMetalLayerRenderStats();
            Operation(gpu,method,gd.get(),dr,gd.get(),sr);
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.cpuFallbacks==before.cpuFallbacks+(shifted?1:0),"P1A shifted alias policy changed");
            Compare(sd.get(),gd.get(),0,"P1A alias/order");
        }
    }
    auto* remove=gpu->GetRenderMethod("RemoveOpacity"); remove->SetParameterOpa(0,128);
    auto mask=Create(gpu,7,5,TVPTextureFormat::Gray,Image(7,5,1,3));
    auto actual=Create(gpu,17,13,TVPTextureFormat::RGBA,Image(17,13,4,5));
    auto expected=Create(sw,17,13,TVPTextureFormat::RGBA,Image(17,13,4,5));
    mask->GetTextureHandle(); actual->GetTextureHandle();
    for(auto sr:{tTVPRect(0,0,7,5),tTVPRect(7,0,0,5)}) {
        const auto before=TVPGetMetalLayerRenderStats(); bool failed=false;
        try { Operation(gpu,remove,actual.get(),tTVPRect(0,0,11,9),mask.get(),sr); }
        catch(const std::exception&) { failed=true; }
        const auto after=TVPGetMetalLayerRenderStats();
        Require(failed && after.gpuOperations==before.gpuOperations && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "unsafe R8 geometry was not rejected before writes/transfers");
    }
    Compare(expected.get(),actual.get(),0,"R8 rejection preserves target");
    for(auto* name:{"SubBlend","MulBlend","MulBlend_HDA","ColorDodgeBlend","DarkenBlend","LightenBlend","ScreenBlend","AdditiveAlphaToAlpha","AlphaBlend_SD"}) {
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(method->EnumParameterID("opacity"),127);
        auto original=Image(17,13,4,25);
        auto source=Create(gpu,17,13,TVPTextureFormat::RGBA,original);
        auto target=Create(gpu,17,13,TVPTextureFormat::RGBA,original);
        auto unchanged=Create(sw,17,13,TVPTextureFormat::RGBA,original);
        source->GetTextureHandle(); target->GetTextureHandle();
        const auto before=TVPGetMetalLayerRenderStats(); bool failed=false;
        try { Operation(gpu,method,target.get(),tTVPRect(1,1,8,6),source.get(),tTVPRect(8,1,1,6)); }
        catch(const std::exception&) { failed=true; }
        const auto after=TVPGetMetalLayerRenderStats();
        Require(failed && after.gpuOperations==before.gpuOperations && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "unsupported P1A mirror entered GPU or unsafe software fallback");
        TVPLayerOperation op; Require(method->DescribeGpuOperation(op),"missing P1A mirror test descriptor");
        Require(!backend->OperateLayerRect(op,target->GetTextureHandle(),TVPLayerRect{1,1,8,6},
                source->GetTextureHandle(),TVPLayerRect{8,1,1,6},0),"backend accepted unsupported P1A mirror");
        Compare(unchanged.get(),target.get(),0,"P1A mirror rejection preserves target");
        auto wrongSource=Create(gpu,17,13,TVPTextureFormat::Gray,Image(17,13,1,7));
        const auto formatBefore=TVPGetMetalLayerRenderStats(); failed=false;
        try { Operation(gpu,method,target.get(),tTVPRect(1,1,8,6),wrongSource.get(),tTVPRect(1,1,8,6)); }
        catch(const std::exception&) { failed=true; }
        const auto formatAfter=TVPGetMetalLayerRenderStats();
        Require(failed && formatAfter.cpuFallbacks==formatBefore.cpuFallbacks &&
                formatAfter.readbackBytes==formatBefore.readbackBytes && formatAfter.uploadedBytes==formatBefore.uploadedBytes,
                "invalid P1A source format reached an unsafe 32-bit fallback");
        Compare(unchanged.get(),target.get(),0,"P1A format rejection preserves target");
    }
    for(auto* name:{"AdjustGamma","AdjustGamma_a"}) {
        auto* method=gpu->GetRenderMethod(name);
        const tTVPGLGammaAdjustData data{1.3f,7,239,0.6f,3,219,2.1f,17,227};
        method->SetParameterPtr(method->EnumParameterID("gammaAdjustData"),&data);
        auto pixels=Image(17,13,4,71); const uint32_t changed=0x40102030;
        std::memcpy(pixels.data()+(2*17+2)*4,&changed,4);
        auto sd=Create(sw,17,13,TVPTextureFormat::RGBA,pixels);
        auto gd=Create(gpu,17,13,TVPTextureFormat::RGBA,Image(17,13,4,71));
        auto* pointer=static_cast<uint32_t*>(gd->GetPersistentCPUData(true));
        pointer[2*17+2]=changed;
        const tTVPRect roi(1,1,7,6);
        sw->OperateRect(method,sd.get(),nullptr,roi,tRenderTexRectArray());
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateRect(method,gd.get(),nullptr,roi,tRenderTexRectArray());
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.cpuFallbacks==before.cpuFallbacks+1 && after.gammaLUTUploads==before.gammaLUTUploads &&
                pointer==gd->GetPersistentCPUData(true),"Gamma broke pinned CPU pointer/fallback semantics");
        Compare(sd.get(),gd.get(),0,"Gamma CPU pin/lease fallback");
        gd->ReleasePersistentCPUData(nullptr);
    }
#ifndef TEST_NATIVE_METAL
    // Inject a native-resource failure before writes, then compare CPU fallback
    // against the initialized software method with the same owned parameters.
    auto* doubleBackend=static_cast<DeviceDouble*>(backend);
    doubleBackend->rejectGamma=true;
    auto* gamma=gpu->GetRenderMethod("AdjustGamma");
    const tTVPGLGammaAdjustData data{1.7f,11,231,0.7f,2,247,2.3f,19,213};
    gamma->SetParameterPtr(gamma->EnumParameterID("gammaAdjustData"),&data);
    actual=Create(gpu,17,13,TVPTextureFormat::RGBA,Image(17,13,4,5));
    expected=Create(sw,17,13,TVPTextureFormat::RGBA,Image(17,13,4,5));
    const tTVPRect rect(2,1,15,12); actual->GetTextureHandle();
    sw->OperateRect(gamma,expected.get(),nullptr,rect,tRenderTexRectArray());
    const auto before=TVPGetMetalLayerRenderStats();
    gpu->OperateRect(gamma,actual.get(),nullptr,rect,tRenderTexRectArray());
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.cpuFallbacks==before.cpuFallbacks+1 && after.gammaLUTUploads==before.gammaLUTUploads,
            "Gamma resource failure did not safely fall back");
    Compare(expected.get(),actual.get(),0,"Gamma resource fallback");
    doubleBackend->rejectGamma=false;
#endif
    std::cout<<"PASS P1A resident rectangles, ROI, mask rejection, alias order and failure routing\n";
}

static void P1BRectangles(iTVPRenderBackend* backend) {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    const char* names[]={"PsAlphaBlend","PsAddBlend","PsSubBlend","PsSoftLightBlend","PsColorDodgeBlend",
        "PsColorBurnBlend","PsLightenBlend","PsDarkenBlend","PsDiffBlend","PsDiff5Blend","PsExclusionBlend"};
    const auto tablesBefore=backend->GetLayerParameterUploadStats();
#ifndef TEST_NATIVE_METAL
    // Force first-session table provisioning to fail before any target writes.
    auto* device=static_cast<DeviceDouble*>(backend); device->rejectPsTables=true;
    auto* soft=gpu->GetRenderMethod("PsSoftLightBlend"); soft->SetParameterOpa(0,128);
    auto pixels=Image(19,15,4,23),destination=Image(19,15,4,47);
    auto ss=Create(sw,19,15,TVPTextureFormat::RGBA,pixels),gs=Create(gpu,19,15,TVPTextureFormat::RGBA,pixels);
    auto sd=Create(sw,19,15,TVPTextureFormat::RGBA,destination),gd=Create(gpu,19,15,TVPTextureFormat::RGBA,destination);
    gs->GetTextureHandle(); gd->GetTextureHandle();
    const tTVPRect rect(1,1,17,14);
    Operation(sw,soft,sd.get(),rect,ss.get(),rect);
    const auto failureBefore=TVPGetMetalLayerRenderStats();
    Operation(gpu,soft,gd.get(),rect,gs.get(),rect);
    const auto failureAfter=TVPGetMetalLayerRenderStats();
    Require(failureAfter.cpuFallbacks==failureBefore.cpuFallbacks+1 &&
            failureAfter.psTableUploads==failureBefore.psTableUploads &&
            failureAfter.gpuRejectCountByReason[int(TVPLayerGPURejectReason::PsTables)]==
                failureBefore.gpuRejectCountByReason[int(TVPLayerGPURejectReason::PsTables)]+1,
            "PS table failure did not retain exact software fallback/reason");
    Compare(sd.get(),gd.get(),0,"PS table resource fallback");
    device->rejectPsTables=false;
#endif
    for(auto* name:names) for(int opacity:{0,1,63,127,128,191,254,255}) {
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,opacity);
        auto image=Image(19,15,4,71),output=Image(19,15,4,91);
        auto ss=Create(sw,19,15,TVPTextureFormat::RGBA,image),gs=Create(gpu,19,15,TVPTextureFormat::RGBA,image);
        auto sd=Create(sw,19,15,TVPTextureFormat::RGBA,output),gd=Create(gpu,19,15,TVPTextureFormat::RGBA,output);
        gs->GetTextureHandle(); gd->GetTextureHandle();
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
        sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
        const tTVPRect dst(3,2,16,13),src(1,1,14,12);
        Operation(sw,method,sd.get(),dst,ss.get(),src);
        const auto before=TVPGetMetalLayerRenderStats();
        Operation(gpu,method,gd.get(),dst,gs.get(),src);
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "resident PS blend transferred pixels or fell back");
        Compare(sd.get(),gd.get(),0,name);
    }
    const auto tablesAfter=backend->GetLayerParameterUploadStats();
    Require(tablesAfter.psTableUploads>=tablesBefore.psTableUploads &&
            tablesAfter.psTableUploads<=tablesBefore.psTableUploads+1 &&
            tablesAfter.psTableUploadedBytes-tablesBefore.psTableUploadedBytes==
                196608*(tablesAfter.psTableUploads-tablesBefore.psTableUploads),
            "PS tables uploaded per operation or were billed as wrong-sized parameters");
    Require(backend->SetLayerPsTables(TVPGetPsBlendTable(0),TVPGetPsBlendTable(1),TVPGetPsBlendTable(2)),
            "repeat PS table supply failed");
    Require(backend->GetLayerParameterUploadStats().psTableUploads==tablesAfter.psTableUploads,
            "identical PS tables uploaded twice");
    Require(!backend->SetLayerPsTables(nullptr,TVPGetPsBlendTable(1),TVPGetPsBlendTable(2)) &&
            backend->GetLayerParameterUploadStats().psTableUploads==tablesAfter.psTableUploads,
            "invalid PS table supply modified the retained resource");
    for(auto* name:names) {
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,191);
        for(int sampling:{0,1}) {
            std::vector<uint8_t> image(7*5*4); const uint32_t value=0xb79fa3c7;
            for(size_t i=0;i<image.size();i+=4) std::memcpy(image.data()+i,&value,4);
            auto output=Image(19,15,4,41);
            auto ss=Create(sw,7,5,TVPTextureFormat::RGBA,image),gs=Create(gpu,7,5,TVPTextureFormat::RGBA,image);
            auto sd=Create(sw,19,15,TVPTextureFormat::RGBA,output),gd=Create(gpu,19,15,TVPTextureFormat::RGBA,output);
            gs->GetTextureHandle(); gd->GetTextureHandle();
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
            sw->SetParameterInt(sw->EnumParameterID("StretchType"),sampling);
            const tTVPRect dst(-2,-1,17,14),src(0,0,7,5);
            Operation(sw,method,sd.get(),dst,ss.get(),src);
            const auto before=TVPGetMetalLayerRenderStats();
            Operation(gpu,method,gd.get(),dst,gs.get(),src);
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.cpuFallbacks==before.cpuFallbacks && after.readbackBytes==before.readbackBytes &&
                    after.uploadedBytes==before.uploadedBytes,"PS clipped scale left residency");
            Compare(sd.get(),gd.get(),0,"PS uniform-source clip/scale routing");
        }
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
        sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
        for(bool shifted:{false,true}) {
            auto image=Image(19,15,4,17);
            auto sd=Create(sw,19,15,TVPTextureFormat::RGBA,image),gd=Create(gpu,19,15,TVPTextureFormat::RGBA,image);
            const tTVPRect dst(2,2,16,13),src=shifted?tTVPRect(1,1,15,12):dst;
            Operation(sw,method,sd.get(),dst,sd.get(),src);
            const auto before=TVPGetMetalLayerRenderStats();
            Operation(gpu,method,gd.get(),dst,gd.get(),src);
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.cpuFallbacks==before.cpuFallbacks+(shifted?1:0),"PS alias order domain changed");
            Compare(sd.get(),gd.get(),0,"PS alias/order");
        }
        auto image=Image(19,15,4,63);
        auto source=Create(gpu,19,15,TVPTextureFormat::RGBA,image),target=Create(gpu,19,15,TVPTextureFormat::RGBA,image);
        auto unchanged=Create(sw,19,15,TVPTextureFormat::RGBA,image);
        source->GetTextureHandle(); target->GetTextureHandle();
        bool rejected=false; const auto before=TVPGetMetalLayerRenderStats();
        try { Operation(gpu,method,target.get(),tTVPRect(1,1,8,6),source.get(),tTVPRect(8,1,1,6)); }
        catch(const std::exception&) { rejected=true; }
        const auto after=TVPGetMetalLayerRenderStats();
        Require(rejected && after.gpuOperations==before.gpuOperations && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "PS mirror reached an undefined software/GPU domain");
        Compare(unchanged.get(),target.get(),0,"PS mirror rejection");
        auto* pointer=static_cast<uint32_t*>(target->GetPersistentCPUData(true));
        const uint32_t pixel=0x40112233; pointer[2*19+2]=pixel;
        auto expected=image; std::memcpy(expected.data()+(2*19+2)*4,&pixel,4);
        auto sd=Create(sw,19,15,TVPTextureFormat::RGBA,expected);
        const tTVPRect roi(1,1,8,6); Operation(sw,method,sd.get(),roi,source.get(),roi);
        const auto pinBefore=TVPGetMetalLayerRenderStats();
        Operation(gpu,method,target.get(),roi,source.get(),roi);
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==pinBefore.cpuFallbacks+1 &&
                target->GetPersistentCPUData(true)==pointer,"PS blend broke a CPU lease/pointer");
        Compare(sd.get(),target.get(),0,"PS pinned CPU fallback");
        target->ReleasePersistentCPUData(nullptr);
    }
    std::cout<<"PASS P1B residency, immutable PS tables, ROI, alias order, mirrors and CPU leases\n";
}

static void MaskAndBlurOperations() {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    auto image=Image(17,13,4,47),destination=Image(17,13,4,13);
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
    for(auto name:{"DoGrayScale","CopyBlueToAlpha","MultiplyAlpha"})
    for(bool alias:{false,true}) {
        auto gs=Create(gpu,17,13,TVPTextureFormat::RGBA,image),ss=Create(sw,17,13,TVPTextureFormat::RGBA,image);
        auto gd=Create(gpu,17,13,TVPTextureFormat::RGBA,destination),sd=Create(sw,17,13,TVPTextureFormat::RGBA,destination);
        auto* gt=alias ? gs.get() : gd.get(); auto* st=alias ? ss.get() : sd.get();
        auto* method=gpu->GetRenderMethod(name);
        const auto oldAlpha=gt->GetPointAlpha(3,2);
        auto before=TVPGetMetalLayerRenderStats();
        tTVPRect rect(2,1,15,12);
        Operation(sw,method,st,rect,ss.get(),rect);
        Operation(gpu,method,gt,rect,gs.get(),rect);
        auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes,"gray/mask operation fell back or read pixels");
        const auto alpha=gt->GetPointAlpha(3,2);
        after=TVPGetMetalLayerRenderStats();
        if(alias && !std::strcmp(name,"DoGrayScale"))
            Require(alpha==oldAlpha && after.readbackBytes==before.readbackBytes,"gray invalidated preserved alpha");
        else Require(after.readbackBytes==before.readbackBytes+4,"mask/gray retained stale alpha cache");
        Compare(st,gt,0,name);
    }
    // Right/bottom mask extraction leaves the source color bytes intact, even
    // when it aliases the destination. Odd dimensions keep the trailing pixel.
    for(bool bottom:{false,true}) {
        auto gd=Create(gpu,17,13,TVPTextureFormat::RGBA,image),sd=Create(sw,17,13,TVPTextureFormat::RGBA,image);
        tTVPRect dst=bottom ? tTVPRect(0,0,17,6) : tTVPRect(0,0,8,13);
        tTVPRect src=bottom ? tTVPRect(0,6,17,12) : tTVPRect(8,0,16,13);
        auto* method=gpu->GetRenderMethod("CopyBlueToAlpha");
        auto before=TVPGetMetalLayerRenderStats();
        Operation(sw,method,sd.get(),dst,sd.get(),src);
        Operation(gpu,method,gd.get(),dst,gd.get(),src);
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks,"offset blue mask alias fell back");
        Compare(sd.get(),gd.get(),0,"offset blue mask alias");
    }
    for(auto name:{"BoxBlur","BoxBlurAlpha"})
    for(auto area:{tTVPRect(0,0,0,0),tTVPRect(-1,-1,1,1),tTVPRect(-3,-2,3,2),
                   tTVPRect(-2,0,1,0),tTVPRect(0,-1,0,3),tTVPRect(-30,-20,30,20)})
    for(auto rect:{tTVPRect(0,0,17,13),tTVPRect(3,2,15,12),tTVPRect(4,3,5,12),tTVPRect(4,3,15,4)})
    for(bool alias:{false,true}) {
        auto gs=Create(gpu,17,13,TVPTextureFormat::RGBA,image),ss=Create(sw,17,13,TVPTextureFormat::RGBA,image);
        auto gd=Create(gpu,17,13,TVPTextureFormat::RGBA,destination),sd=Create(sw,17,13,TVPTextureFormat::RGBA,destination);
        auto* gt=alias ? gs.get() : gd.get(); auto* st=alias ? ss.get() : sd.get();
        auto* method=gpu->GetRenderMethod(name);
        method->SetParameterInt(method->EnumParameterID("area_left"),area.left);
        method->SetParameterInt(method->EnumParameterID("area_top"),area.top);
        method->SetParameterInt(method->EnumParameterID("area_right"),area.right);
        method->SetParameterInt(method->EnumParameterID("area_bottom"),area.bottom);
        const auto before=TVPGetMetalLayerRenderStats();
        Operation(sw,method,st,rect,ss.get(),rect);
        Operation(gpu,method,gt,rect,gs.get(),rect);
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "box blur introduced a CPU transfer");
        Compare(st,gt,0,name);
    }
}
static void RectangleStretchModes() {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    for(int mode:{0,1,2,3,4,13,0x10000,0x10002})
    for(bool scale:{false,true}) for(auto name:{"Copy","CopyColor","AlphaBlend_d","PsColorDodge5Blend"}) {
        auto src=Image(9,7,4,48),dst=Image(13,11,4,22);
        auto gs=Create(gpu,9,7,TVPTextureFormat::RGBA,src),ss=Create(sw,9,7,TVPTextureFormat::RGBA,src);
        auto gd=Create(gpu,13,11,TVPTextureFormat::RGBA,dst),sd=Create(sw,13,11,TVPTextureFormat::RGBA,dst);
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,128);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),mode);
        auto rect=scale ? tTVPRect(-1,-2,12,10) : tTVPRect(1,2,10,9);
        const auto before=TVPGetMetalLayerRenderStats();
        Operation(sw,method,sd.get(),rect,ss.get(),tTVPRect(0,0,9,7));
        Operation(gpu,method,gd.get(),rect,gs.get(),tTVPRect(0,0,9,7));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.cpuFallbacks==before.cpuFallbacks && after.readbackBytes==before.readbackBytes,
                "ordinary rectangle rejected software-compatible stretch mode");
        Compare(sd.get(),gd.get(),scale && mode!=0 ? 1 : 0,"rectangle stretch semantics");
        auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x4080a0c0);
        const auto beforeFill=TVPGetMetalLayerRenderStats();
        Operation(gpu,fill,gd.get(),tTVPRect(0,0,13,11),nullptr,tTVPRect());
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==beforeFill.cpuFallbacks,"fill inherited irrelevant stretch rejection");
    }
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
}

static void ExtendedBlendGeometry() {
    auto* gpu=TVPGetRenderManager(); auto* sw=TVPGetSoftwareRenderManager();
    for(auto name:{"AdditiveAlphaBlend","AdditiveAlphaBlend_a","PsMulBlend","PsOverlayBlend","PsHardLightBlend","PsScreenBlend","PsColorDodge5Blend","AddBlend"})
    for(int opacity:{0,128,255}) {
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,opacity);
        auto image=Image(7,5,4,42),destination=Image(11,9,4,12);
        for(int sampling:{0,1,2}) {
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
            sw->SetParameterInt(sw->EnumParameterID("StretchType"),sampling);
            auto gs=Create(gpu,7,5,TVPTextureFormat::RGBA,image),ss=Create(sw,7,5,TVPTextureFormat::RGBA,image);
            for(auto rect:{tTVPRect(-2,-1,9,8),tTVPRect(2,1,13,11),tTVPRect(4,4,5,5)}) {
                auto gd=Create(gpu,11,9,TVPTextureFormat::RGBA,destination),sd=Create(sw,11,9,TVPTextureFormat::RGBA,destination);
                auto before=TVPGetMetalLayerRenderStats();
                Operation(sw,method,sd.get(),rect,ss.get(),tTVPRect(0,0,7,5));
                Operation(gpu,method,gd.get(),rect,gs.get(),tTVPRect(0,0,7,5));
                auto after=TVPGetMetalLayerRenderStats();
                Require(after.cpuFallbacks==before.cpuFallbacks && after.readbackBytes==before.readbackBytes,
                        "extended clipped/scaled blend fell back or read pixels");
                Compare(sd.get(),gd.get(),sampling ? 2 : 0,"extended blend clip/scale");
            }
        }
        for(bool offset:{false,true}) {
            auto gd=Create(gpu,11,9,TVPTextureFormat::RGBA,destination),sd=Create(sw,11,9,TVPTextureFormat::RGBA,destination);
            tTVPRect dst(2,1,10,8),src=offset ? tTVPRect(0,0,8,7) : dst;
            auto before=TVPGetMetalLayerRenderStats();
            Operation(sw,method,sd.get(),dst,sd.get(),src);
            Operation(gpu,method,gd.get(),dst,gd.get(),src);
            Require(TVPGetMetalLayerRenderStats().cpuFallbacks-before.cpuFallbacks==uint64_t(offset),
                    "self-blend alias routing changed software ordering");
            Compare(sd.get(),gd.get(),0,"extended self-blend");
        }
    }
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    sw->SetParameterInt(sw->EnumParameterID("StretchType"),0);
    for(auto name:{"AdditiveAlphaBlend","PsMulBlend","PsOverlayBlend","PsHardLightBlend","AdditiveAlphaBlend_a"}) {
        auto image=Image(7,5,4,81);
        auto gd=Create(gpu,7,5,TVPTextureFormat::RGBA,image),gs=Create(gpu,7,5,TVPTextureFormat::RGBA,image);
        const auto alpha=gd->GetPointAlpha(2,1); auto before=TVPGetMetalLayerRenderStats();
        auto* method=gpu->GetRenderMethod(name); method->SetParameterOpa(0,128);
        Operation(gpu,method,gd.get(),tTVPRect(0,0,7,5),gs.get(),tTVPRect(0,0,7,5));
        const auto result=gd->GetPointAlpha(2,1); auto after=TVPGetMetalLayerRenderStats();
        if(std::strcmp(name,"AdditiveAlphaBlend_a")) {
            Require(result==alpha && after.readbackBytes==before.readbackBytes,"HDA blend invalidated preserved alpha cache");
        } else Require(after.readbackBytes==before.readbackBytes+4,"premultiplied blend retained stale alpha cache");
    }
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
        auto* gray=gpu->GetRenderMethod("PsAlphaBlend");
        Operation(gpu,gray,t.get(),tTVPRect(1,1,16,12),t.get(),tTVPRect(0,0,15,11));
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
    const char* methods[]={"ConstAlphaBlend_SD","ConstAlphaBlend_SD_d","ConstAlphaBlend_SD_a"};
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
static void AffineCopyTriangles() {
    auto* sw=TVPGetSoftwareRenderManager(); auto* gpu=TVPGetRenderManager();
    auto* copy=gpu->GetRenderMethod("Copy");
    const int w=31,h=23;
    // Pixel-center matrix coordinates, fractional translation, scale, rotation,
    // shear, combined transforms, and clockwise/counterclockwise winding.
    const double matrices[][6]={
        {1,0,0,1,0,0},{1,0,0,1,3,2},{1,0,0,1,2.3,-1.7},
        {1.7,0,0,0.6,1,2},{0.8,0.6,-0.6,0.8,10,1},
        {0.6,-0.8,0.8,0.6,2,16},{1,0.25,0.35,1,2,1},
        {-0.8,0.6,0.6,0.8,19,1},{0,-1,1,0,2,20},
        {0.53,0.27,-0.42,1.33,12.2,-2.4}};
    const tTVPRect clips[]={tTVPRect(0,0,w,h),tTVPRect(3,2,24,19),tTVPRect(27,20,31,23)};
    const tTVPRect rects[]={tTVPRect(0,0,17,13),tTVPRect(3,2,14,10)};
    int cases=0;
    for(const auto& m:matrices) for(const auto& clip:clips) for(const auto& sr:rects)
    for(int sampling=0;sampling<3;++sampling) for(int alias=0;alias<2;++alias) {
        auto initial=Image(w,h,4,5),source=Image(w,h,4,2);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,source),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,source);
        tTVPPointD sp[]={{double(sr.left),double(sr.top)},{double(sr.right),double(sr.top)},
                        {double(sr.left),double(sr.bottom)},{double(sr.right),double(sr.top)},
                        {double(sr.left),double(sr.bottom)},{double(sr.right),double(sr.bottom)}};
        auto transform=[&](double x,double y) { return tTVPPointD{m[0]*x+m[2]*y+m[4],m[1]*x+m[3]*y+m[5]}; };
        tTVPPointD dp[6]; dp[0]=transform(-0.5,-0.5);dp[1]=transform(sr.get_width()-0.5,-0.5);
        dp[2]=transform(-0.5,sr.get_height()-0.5);dp[3]=dp[1];dp[4]=dp[2];
        dp[5]={dp[1].x-dp[0].x+dp[2].x,dp[1].y-dp[0].y+dp[2].y};
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(alias?expected.get():ss.get(),sp),gi(alias?actual.get():gs.get(),sp);
        // Warm handles before accounting; successful operations must stay resident.
        actual->GetTextureHandle(); gs->GetTextureHandle();
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
        sw->OperateTriangles(copy,2,expected.get(),expected.get(),clip,dp,tRenderTexQuadArray(&si,1));
        const auto before=TVPGetMetalLayerRenderStats();
        gpu->OperateTriangles(copy,2,actual.get(),actual.get(),clip,dp,tRenderTexQuadArray(&gi,1));
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks,
                "supported affine Copy did not stay on GPU");
        Require(after.uploadedBytes==before.uploadedBytes && after.readbackBytes==before.readbackBytes,
                "GPU affine Copy transferred CPU pixels");
        // Software rectangular self-copy is overlap-sensitive; compare against
        // its pre-operation snapshot to define the safe alias behavior.
        if(alias && (m[1]==0 && m[2]==0)) {
            expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
            auto snapshot=Create(sw,w,h,TVPTextureFormat::RGBA,initial);
            si.first=snapshot.get();
            sw->OperateTriangles(copy,2,expected.get(),expected.get(),clip,dp,tRenderTexQuadArray(&si,1));
        }
        Compare(expected.get(),actual.get(),0,"Copy affine triangle"); ++cases;
    }
    for(const auto& size:std::vector<std::pair<int,int>>{{1,1},{1,7},{7,1}}) {
        const int a=size.first,b=size.second;
        auto image=Image(a,b,4,3),initial=Image(w,h,4,5);
        auto ss=Create(sw,a,b,TVPTextureFormat::RGBA,image),gs=Create(gpu,a,b,TVPTextureFormat::RGBA,image);
        tTVPPointD sp[]={{0,0},{double(a),0},{0,double(b)},{double(a),0},{0,double(b)},{double(a),double(b)}};
        tTVPPointD dp[]={{2,3},{12,4},{3,16},{12,4},{3,16},{13,17}};
        for(int sampling=0;sampling<3;++sampling) {
            auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,initial),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,initial);
            std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),sp),gi(gs.get(),sp);
            gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),sampling);
            auto before=TVPGetMetalLayerRenderStats();
            sw->OperateTriangles(copy,2,expected.get(),nullptr,tTVPRect(0,0,w,h),dp,tRenderTexQuadArray(&si,1));
            gpu->OperateTriangles(copy,2,actual.get(),nullptr,tTVPRect(0,0,w,h),dp,tRenderTexQuadArray(&gi,1));
            Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks,"single-dimension affine fell back");
            Compare(expected.get(),actual.get(),0,"single-dimension affine"); ++cases;
        }
    }
    // Unsupported methods/samplers and mirrored scanline geometry must still
    // use software. Fractional source rectangles remain covered by profile tests.
    for(int failure=0;failure<3;++failure) {
        auto image=Image(w,h,4,3);
        auto expected=Create(sw,w,h,TVPTextureFormat::RGBA,image),actual=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        auto ss=Create(sw,w,h,TVPTextureFormat::RGBA,image),gs=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
        tTVPPointD sp[]={{1,1},{14,1},{1,10},{14,1},{1,10},{14,10}};
        tTVPPointD dp[]={{2,2},{19,2},{2,14},{19,2},{2,14},{19,14}};
        if(failure==2) {dp[0].x=dp[2].x=dp[4].x=19;dp[1].x=dp[3].x=dp[5].x=2;}
        auto* method=gpu->GetRenderMethod(failure==0?"DoGrayScale":"Copy"); method->SetParameterOpa(0,127);
        gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),failure==1?3:1);
        std::pair<iTVPTexture2D*,const tTVPPointD*> si(ss.get(),sp),gi(gs.get(),sp);
        const auto before=TVPGetMetalLayerRenderStats();
        sw->OperateTriangles(method,2,expected.get(),nullptr,tTVPRect(0,0,w,h),dp,tRenderTexQuadArray(&si,1));
        gpu->OperateTriangles(method,2,actual.get(),nullptr,tTVPRect(0,0,w,h),dp,tRenderTexQuadArray(&gi,1));
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks+1,"unsupported affine did not fall back");
        Compare(expected.get(),actual.get(),0,"unsupported affine fallback");
    }
    // A warmed full-HD burst must not gain one submit/wait per operation.
    auto image=Image(1920,1080,4,2);
    auto target=Create(gpu,1920,1080,TVPTextureFormat::RGBA,image),source=Create(gpu,1920,1080,TVPTextureFormat::RGBA,image);
    target->GetTextureHandle();source->GetTextureHandle();
    tTVPPointD sp[]={{0,0},{1920,0},{0,1080},{1920,0},{0,1080},{1920,1080}};
    tTVPPointD dp[]={{1,0},{1920,30},{-30,1080},{1920,30},{-30,1080},{1889,1110}};
    std::pair<iTVPTexture2D*,const tTVPPointD*> input(source.get(),sp);
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),1);
    TVPTakeMetalLayerTriangleProfile();
    const auto before=TVPGetMetalLayerRenderStats();
#ifdef TEST_NATIVE_METAL
    const auto submits=testMetalSubmits,waits=testMetalWaits;
#endif
    for(int i=0;i<24;++i) gpu->OperateTriangles(copy,2,target.get(),target.get(),tTVPRect(0,0,1920,1080),dp,tRenderTexQuadArray(&input,1));
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.gpuOperations==before.gpuOperations+24 && after.cpuFallbacks==before.cpuFallbacks &&
            after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
            "full-HD triangle burst transferred CPU pixels or fell back");
    const auto profile=TVPTakeMetalLayerTriangleProfile();
    Require(profile.stats.gpuCalls==24 && profile.stats.gpuPixels==24ULL*1920*1080 && profile.stats.calls==0,
            "successful triangle interval counters did not reach sampler");
    Require(TVPTakeMetalLayerTriangleProfile().stats.gpuCalls==0,"triangle GPU interval did not reset");
#ifdef TEST_NATIVE_METAL
    Require(testMetalSubmits-submits<=1 && testMetalWaits==waits,"triangle burst submitted/waited per operation");
#endif
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),0);
    std::cout<<"PASS Copy affine triangle: "<<cases<<" exact comparisons and full-HD resident burst\n";
}
#include "P2AAffineTests.inc"
#include "P2BPerspectiveTests.inc"
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
    Compare(expected.get(),actual.get(),0,"perspective Copy supported rectangle");
    gpu->SetParameterInt(gpu->EnumParameterID("StretchType"),3);
    Operation(sw,copy,expected.get(),tTVPRect(1,1,8,6),ss.get(),tTVPRect(0,0,9,7));
    Operation(gpu,copy,actual.get(),tTVPRect(1,1,8,6),gs.get(),tTVPRect(0,0,9,7));
    Compare(expected.get(),actual.get(),0,"software-compatible cubic rectangle");
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
static void FullHDOverdrawWorkload(iTVPRenderBackend* backend) {
    auto* gpu=TVPGetRenderManager();
    constexpr int w=1920,h=1080,frames=30,layers=20;
    auto image=Image(w,h,4,31);
    auto source=Create(gpu,w,h,TVPTextureFormat::RGBA,image),target=Create(gpu,w,h,TVPTextureFormat::RGBA,image);
    source->GetTextureHandle(); target->GetPoint(0,0); // Complete initial uploads before measurement.
    auto* fill=gpu->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x11223344);
    auto* blend=gpu->GetRenderMethod("AlphaBlend_d"); blend->SetParameterOpa(0,191);
    const auto before=TVPGetMetalLayerRenderStats();
    const auto start=std::chrono::steady_clock::now();
    for(int frame=0;frame<frames;++frame) {
        Operation(gpu,fill,target.get(),tTVPRect(0,0,w,h),nullptr,tTVPRect());
        for(int layer=0;layer<layers;++layer) Operation(gpu,blend,target.get(),tTVPRect(0,0,w,h),source.get(),tTVPRect(0,0,w,h));
    }
    const auto stats=TVPGetMetalLayerRenderStats();
    Require(stats.cpuFallbacks==before.cpuFallbacks && stats.readbackBytes==before.readbackBytes &&
            stats.uploadedBytes==before.uploadedBytes,"full-HD overdraw introduced CPU transfers");
    const auto actual=target->GetPoint(0,0); // Wait for all commands, including the final render pass.
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    uint32_t expected=0x11223344,src; std::memcpy(&src,image.data(),4);
    for(int i=0;i<layers;++i) TVPAlphaBlend_do(&expected,&src,1,191);
    Require(actual==expected,"batched render pass did not preserve sequential byte rounding");
    std::cout<<"PASS full-HD 20-layer overdraw frames="<<frames<<" wallMS="<<ms;
#ifdef TEST_NATIVE_METAL
    std::cout<<" tile="<<static_cast<krkrsdl3::MetalRenderBackend*>(backend)->IsLayerTileRenderingActive();
#endif
    std::cout<<'\n';
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

static void InvalidOperationKinds(iTVPRenderBackend& backend) {
    auto pixels=Image(5,3,4,41);
    auto expected=Create(TVPGetSoftwareRenderManager(),5,3,TVPTextureFormat::RGBA,pixels);
    auto actual=Create(TVPGetRenderManager(),5,3,TVPTextureFormat::RGBA,pixels);
    void* target=actual->GetTextureHandle();
    CapabilityAuditTests(true); // Also audit with live, resident GPU resources.
    const TVPLayerRect rect{0,0,5,3};
    TVPLayerAffineCopy affine; affine.clip=affine.sourceCrop=rect;
    TVPLayerPerspectiveQuad perspective;perspective.clip=rect;perspective.inverse[0]=perspective.inverse[4]=perspective.inverse[8]=1;
#ifdef TEST_NATIVE_METAL
    const auto submits=testMetalSubmits, waits=testMetalWaits, blits=testMetalBlits;
#endif
    for(auto kind:{TVPLayerOperationKind::Unsupported,TVPLayerOperationKind::Count,
                   static_cast<TVPLayerOperationKind>(UINT32_MAX)}) {
        TVPLayerOperation op; op.kind=kind;
        Require(!backend.OperateLayerRect(op,target,rect,target,rect,0),"invalid kind encoded a rectangle");
        Require(!backend.OperateLayerAffine(op,target,affine,target,0),"invalid kind encoded affine");
        Require(!backend.OperateLayerPerspective(op,target,&perspective,1,target,0),"invalid kind encoded perspective");
        Require(!backend.OperateLayerRectDualSource(op,target,rect,target,rect,target,rect),"invalid kind encoded dual-source");
        Require(!backend.OperateLayerRectTripleSource(op,target,rect,target,rect,target,rect,target,rect),"invalid kind encoded triple-source");
    }
    for(auto kind:{TVPLayerOperationKind::ConstAlphaSD,TVPLayerOperationKind::UnivTrans}) {
        TVPLayerOperation op; op.kind=kind;
        Require(!backend.OperateLayerRect(op,target,rect,target,rect,0),"multi-source kind encoded through single-source entry");
    }
    for(unsigned failure=0;failure<7;++failure) {
        TVPLayerOperation op;op.kind=TVPLayerOperationKind::Alpha;
        int sampling=0;
        if(failure==0) op.kind=TVPLayerOperationKind::GrayScale;
        if(failure==1) op.flags=16;
        if(failure==2) op.opacity=-1;
        if(failure==3) op.opacity=256;
        if(failure==4) {op.kind=TVPLayerOperationKind::Copy;op.flags=TVP_LAYER_HOLD_ALPHA;}
        if(failure==5) sampling=-1;
        if(failure==6) sampling=2;
        Require(!backend.OperateLayerAffine(op,target,affine,target,sampling),"unsupported affine backend domain encoded");
    }
    for(unsigned failure=0;failure<7;++failure) {
        TVPLayerOperation op;op.kind=TVPLayerOperationKind::Alpha;
        auto malformed=affine; malformed.inverse[0]=malformed.inverse[4]=1;
        if(failure==0) malformed.inverse[0]=std::numeric_limits<double>::quiet_NaN();
        if(failure==1) malformed.inverse[2]=std::numeric_limits<double>::infinity();
        if(failure==2) malformed.inverse[0]=1000001;
        if(failure==3) malformed.inverse[4]=0;
        if(failure==4) malformed.sourceCrop.right=malformed.sourceCrop.left;
        if(failure==5) malformed.clip.left=-1;
        if(failure==6) malformed.clip.right=6;
        Require(!backend.OperateLayerAffine(op,target,malformed,target,0),"malformed affine backend map encoded");
    }
#ifdef TEST_NATIVE_METAL
    Require(testMetalSubmits==submits && testMetalWaits==waits && testMetalBlits==blits,
            "invalid operation kind submitted/waited/copied before rejection");
#endif
    Compare(expected.get(),actual.get(),0,"invalid kind leaves target unchanged");
}

static int AuditCapabilities() {
    CapabilityAuditTests(false);
    std::unique_ptr<iTVPRenderBackend> backend;
#ifdef TEST_NATIVE_METAL
    const bool initialized=SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window=initialized ? SDL_CreateWindow("Layer capability audit",128,128,SDL_WINDOW_METAL|SDL_WINDOW_HIDDEN) : nullptr;
    if(window) backend.reset(krkrsdl3::MetalRenderBackend::Create(window,false));
    constexpr bool nativeCompiled=true, deviceDouble=false;
#else
    backend=std::make_unique<DeviceDouble>();
    constexpr bool nativeCompiled=false, deviceDouble=true;
#endif
    const bool available=backend && TVPBindMetalLayerRenderManager(backend.get());
    CapabilityAuditTests(available);
    const auto json=TVPTestCapabilityAuditJSON(nativeCompiled,available,deviceDouble);
    if(available) TVPUnbindMetalLayerRenderManager();
    backend.reset();
#ifdef TEST_NATIVE_METAL
    if(window) SDL_DestroyWindow(window);
    if(initialized) SDL_Quit();
#endif
    std::cout<<json<<'\n';
    return 0;
}

int main(int argc,char** argv) {
    try {
        TVPInitTVPGL(); TVPGetRenderManager(ttstr("software"));
        if(argc>1 && std::string(argv[1])=="--audit-capabilities") return AuditCapabilities();
        const bool sessionCachesOnly=argc>1 && std::string(argv[1])=="--session-caches";
        if(!sessionCachesOnly) {
            CapabilityAuditTests(false); CompilationFailureTests(); OperationContractTests();
            UnivTransShaderTests(); LayerBlendShaderTests(); P1AShaderTests(); P1BShaderTests(); TransitionContractTests(); ShrinkShaderContractTests();
            RunC2ConsumerTraceTests();
            Require(RunC2LayerExBoundaryTests()==0,"C2A production LayerEx boundary tests failed");
        }
        std::unique_ptr<iTVPRenderBackend> backend;
#ifdef TEST_NATIVE_METAL
        Require(SDL_Init(SDL_INIT_VIDEO),"SDL video init failed");
        SDL_Window* window=SDL_CreateWindow("Metal Layer tests",128,128,SDL_WINDOW_METAL|SDL_WINDOW_HIDDEN);
        if(!window) { std::cout<<"SKIP no native Metal window\n"; SDL_Quit(); return 77; }
        backend.reset(krkrsdl3::MetalRenderBackend::Create(window,false));
        Require(bool(backend),"native Metal init failed");
        auto* native=static_cast<krkrsdl3::MetalRenderBackend*>(backend.get());
        if(native->SupportsLayerTileRendering()) Require(native->IsLayerTileRenderingActive()==
            SDL_GetHintBoolean("MIKAGE_METAL_LAYER_TILE_RENDERER",true),"Apple GPU tile pipeline failed to initialize");
#else
        backend=std::make_unique<DeviceDouble>();
#endif
        if(sessionCachesOnly) {
            BitmapRenderSessionCacheTests(backend.get()); backend.reset();
#ifdef TEST_NATIVE_METAL
            SDL_DestroyWindow(window); SDL_Quit();
#endif
            std::cout<<"PASS production retained bitmap/glyph caches across four render sessions\n";
            return 0;
        }
#ifndef TEST_NATIVE_METAL
        class UnavailableDevice final : public DeviceDouble {
            void* CreateLayerTexture(int,int,TVPLayerTextureFormat) override { return nullptr; }
        } unavailable;
        Require(!TVPBindMetalLayerRenderManager(&unavailable) && TVPIsSoftwareRenderManager() && std::strlen(TVPMetalLayerFallbackReason()),"resource init did not retain software composition");
#endif
        if(argc>1 && std::string(argv[1])=="--performance") {
            Require(TVPBindMetalLayerRenderManager(backend.get()),"GPU Layer init failed");
            CompositionWorkload(); GlyphWorkload();
#ifdef TEST_NATIVE_METAL
            FullHDOverdrawWorkload(backend.get());
#endif
            TVPUnbindMetalLayerRenderManager(); backend.reset();
#ifdef TEST_NATIVE_METAL
            SDL_DestroyWindow(window);SDL_Quit();
#endif
            return 0;
        }
        // Keep C4's necessary alpha-table supply in its own session, so the
        // existing cold-table rejection fixtures still exercise a cold session.
        Require(TVPBindMetalLayerRenderManager(backend.get()),"transition session init failed");
        TransitionHandlerTests(backend.get()); C4ProfileTests(); C4BitmapResourceTests(backend.get());
#ifndef TEST_NATIVE_METAL
        auto& transitionDevice=*static_cast<DeviceDouble*>(backend.get());
        TransitionHandlerDispatchFailureTests([&](bool value){transitionDevice.throwAfterTransitionDispatch=value;});
        TransitionHandlerPipelineFailureTests([&](bool value){transitionDevice.rejectTransition=value;});
#endif
        TVPUnbindMetalLayerRenderManager();
        Require(TVPBindMetalLayerRenderManager(backend.get()),"shrink session init failed");
        C2ProductionDiagnostics(backend.get());
        ShrinkCopyTests(backend.get()); C1ProfileTests();
#ifndef TEST_NATIVE_METAL
        auto& shrinkDevice=*static_cast<DeviceDouble*>(backend.get());
        ShrinkCopyFailureTests([&](int stage,bool wide){shrinkDevice.shrinkFailureStage=stage;shrinkDevice.shrinkWideAvailable=wide;});
#endif
        TVPUnbindMetalLayerRenderManager();
        auto* cached=TVPGetSoftwareRenderManager()->GetRenderMethod("AlphaBlend_d");
        for(int session=0;session<3;++session) {
            TVPSetMetalLayerTriangleDiagnostics(true);
            Require(TVPBindMetalLayerRenderManager(backend.get()),"GPU Layer init failed");
            CapabilityAuditTests(true);
            if(session==0) InvalidOperationKinds(*backend);
            const auto newTriangleInterval=TVPTakeMetalLayerTriangleProfile();
            Require(newTriangleInterval.stats.calls==0 && newTriangleInterval.stats.maxCpuTimeNS==0 &&
                    newTriangleInterval.methods.empty(),"triangle profile leaked across Layer sessions");
            Require(cached==TVPGetRenderManager()->GetRenderMethod("AlphaBlend_d"),"method lifetime changed");
#ifndef TEST_NATIVE_METAL
            AsyncLayerAlpha(*static_cast<DeviceDouble*>(backend.get()));
#endif
            {
                auto pixels=Image(9,7,4,2);auto texture=Create(TVPGetRenderManager(),9,7,TVPTextureFormat::RGBA,pixels);
                std::vector<uint8_t> region;int pitch=0;
                Require(backend->ReadLayerTextureRegion(texture->GetTextureHandle(),TVPLayerRect{2,3,5,5},region,pitch) && pitch==12 && region.size()==24,"local RGBA readback failed");
                for(int y=0;y<2;++y) Require(!std::memcmp(region.data()+y*pitch,pixels.data()+((y+3)*9+2)*4,12),"local readback pixels differ");
            }
            P2BResourceFailures(backend.get(),true); P2AAffineAlphaTableFallback(backend.get()); Equivalence(); DualSourceTransitions(); UnivTransTransitions(); UnivTransFallbacks(backend.get()); UnivTransClippedFallbacks(backend.get()); OffsetUpdates(); Synchronization(); ExactHitTestCache(); PointReadAttribution(); UIPointWaitCounters(); DirtyRegionUploads(); ScopedNativePixels(); AlphaConversionReference(); P1ARectangles(backend.get()); P1AGammaTests(backend.get()); P2BResourceFailures(backend.get(),false); P2AAffineTableFallbacks(backend.get()); P1BRectangles(backend.get()); ExtendedBlendGeometry(); MaskAndBlurOperations(); RectangleStretchModes(); WorkDiagnostics(); C0ProfileTests(); C0ProductionDiagnostics(backend.get()); OverwriteSkipsReadback(); BitmapOverwriteTests(backend.get()); TransitionOutputTests(); ReadbackAttribution(); Compatibility(); AffineCopyTriangles(); P2AAffineBlends(backend.get()); P2BSoftwareAndMath(); P2BPerspectives(backend.get()); P2BRejectsAndLifetimes(backend.get()); P2BQueuedBatches(); TriangleProfileTests(); CompositionWorkload(); GlyphWorkload();
#ifdef TEST_NATIVE_METAL
            Presentation(backend.get());
#endif
            iTVPTexture2D::RecycleProcess();
            Require(TVPGetMetalLayerRenderStats().gpuResidentBytes==0,"session GPU texture leak");
            TVPUnbindMetalLayerRenderManager(); Require(TVPIsSoftwareRenderManager(),"software manager not restored");
            Require(TVPTakeMetalLayerTriangleProfile().stats.calls==0,"triangle sampler retained an unbound Layer session");
        }
        BitmapRenderSessionCacheTests(backend.get());
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
