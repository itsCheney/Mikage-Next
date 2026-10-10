#include "tjsCommHead.h"
#include "LayerBitmap.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "TVPCompositor.h"
#include "TVPTrans.h"
#include "metaltransition.h"
#include "TVPEvent.h"
#include "TVPMsg.h"
#include "CharacterData.h"
#include "gl/tvpgl.h"
#include "tjsUtils.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <vector>

// Only font/platform setup is stubbed. The transaction and texture adoption
// below are extracted from production and use the real bitmap declaration,
// reference counts, GPU textures, CPU caches, and render manager.
void TVPInializeFontRasterizers() {}
tTVPNativeBaseBitmap::tTVPNativeBaseBitmap()
    : Font{}, FontChanged(true), GlobalFontState(-1), PrerenderedFont(nullptr),
      Bitmap(nullptr) {}
tTVPNativeBaseBitmap::~tTVPNativeBaseBitmap() {
    if(Bitmap) Bitmap->Release();
}
// Unrelated bitmap drawing entries complete the base vtable; these tests use
// the production render manager directly and must never call either boundary.
bool iTVPBaseBitmap::CopyRect(tjs_int,tjs_int,const iTVPBaseBitmap*,tTVPRect,tjs_int) {
    throw std::runtime_error("unexpected fixture CopyRect");
}
// Callback registration is a lifecycle boundary double; bitmap/cache logic
// below is extracted from the production functions.
static std::vector<tTVPCompactEventCallbackIntf*> testCompactHooks;
void TVPAddCompactEventHook(tTVPCompactEventCallbackIntf* hook,bool) {
    if(std::find(testCompactHooks.begin(),testCompactHooks.end(),hook)==testCompactHooks.end()) testCompactHooks.push_back(hook);
}
void TVPRemoveCompactEventHook(tTVPCompactEventCallbackIntf* hook) {
    testCompactHooks.erase(std::remove(testCompactHooks.begin(),testCompactHooks.end(),hook),testCompactHooks.end());
}
// Window/script event machinery is outside this test. The layer facade keeps
// only the two fields used by the exact production copy methods below.
class TestLayerCopy {
public:
    tTVPNativeBaseBitmap* MainImage;
    bool ImageModified=false;
    iTVPTexture2D* GetMainImageTextureForCPUAccess(bool);
    iTVPTexture2D* GetMainImageTextureForSpanComposite();
    bool CopyMainImageFromGPUTarget(krkrsdl3::iTVPRenderBackend*,void*,tjs_int,tjs_int);
    bool CopyMainImageFromGPUTargetRegion(krkrsdl3::iTVPRenderBackend*,void*,tjs_int,tjs_int,const tTVPRect&);
    bool CopyMainImageFromCPU(const void*,tjs_int,tjs_int,tjs_int);
};

#include "ProductionBitmapOverwrite.inc"

namespace {
using krkrsdl3::iTVPRenderBackend;
struct ReleaseTexture {
    void operator()(iTVPTexture2D* texture) const { if(texture) texture->Release(); }
};
using TextureRef=std::unique_ptr<iTVPTexture2D,ReleaseTexture>;
class TestBitmap final : public iTVPBaseBitmap {
public:
    explicit TestBitmap(iTVPTexture2D* texture) { Bitmap=texture; }
    iTVPRenderManager* GetRenderManager() override { return TVPGetRenderManager(); }
    int targetAcquisitions=0;
    iTVPTexture2D* GetTextureForRender(bool blend,const tTVPRect* rect) override {
        ++targetAcquisitions;
        return tTVPNativeBaseBitmap::GetTextureForRender(blend,rect);
    }
    bool BlendGlyph(tTVPCharacterData*,tTVPDrawTextData*,tjs_uint32,const tTVPRect&,tTVPRect&);
    bool DrawGlyphData(tTVPCharacterData*,tjs_int,tjs_int,tjs_uint32,tTVPDrawTextData*,tTVPRect&);
};
#include "ProductionBitmapCaches.inc"
void Require(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}
std::vector<uint32_t> Pixels(int width,int height,uint32_t first) {
    std::vector<uint32_t> pixels(size_t(width)*height);
    for(size_t i=0;i<pixels.size();++i) pixels[i]=first+uint32_t(i)*0x01030507u;
    return pixels;
}
iTVPTexture2D* Create(const std::vector<uint32_t>& pixels,int width,int height,bool immutable=false) {
    auto* manager=TVPGetRenderManager();
    auto* texture=manager->CreateTexture2D(
        immutable ? pixels.data() : nullptr,immutable ? width*4 : 0,
        width,height,TVPTextureFormat::RGBA);
    if(!immutable) texture->Update(pixels.data(),TVPTextureFormat::RGBA,width*4,tTVPRect(0,0,width,height));
    return texture;
}
void Equal(iTVPTexture2D* texture,const std::vector<uint32_t>& expected) {
    const size_t width=texture->GetWidth(),height=texture->GetHeight();
    Require(expected.size()==width*height,"bitmap comparison dimensions differ");
    for(size_t y=0;y<height;++y) {
        const auto* actual=static_cast<const uint32_t*>(texture->GetScanLineForRead(unsigned(y)));
        Require(actual && !std::memcmp(actual,expected.data()+y*width,width*4),
                "bitmap overwrite produced wrong pixels");
    }
}
void NoPreservationWork(const TVPLayerRenderStats& before) {
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.gpuOperations==before.gpuOperations,
            "full overwrite copied discarded old image through layer composition");
    Require(after.readbackBytes==before.readbackBytes,
            "full overwrite read discarded old image to CPU");
    Require(after.uploadedBytes==before.uploadedBytes,
            "full overwrite uploaded discarded CPU pixels");
}
}

void C4BitmapResourceTests(krkrsdl3::iTVPRenderBackend* backend) {
    constexpr int w=8,h=8;
    const auto old=Pixels(w,h,0x12034567),a=Pixels(w,h,0xf02468ac),b=Pixels(w,h,0xabcdef01);
    for(bool diagnostics:{false,true}) for(int mode=0;mode<5;++mode) {
        TestBitmap target(Create(old,w,h)),first(Create(a,w,h)),second(Create(b,w,h));
        target.GetTexture()->GetTextureHandle();first.GetTexture()->GetTextureHandle();second.GetTexture()->GetTextureHandle();
        TextureRef snapshot;
        if(mode==1) {auto* original=target.GetTexture();original->AddRef();snapshot.reset(original);}
        if(mode==2) target.GetTexture()->MarkCPUModified(tTVPRect(0,0,w,h));
        const void* lease=mode==4 ? target.GetTexture()->LockCPURead() : nullptr;
        tTVPScanLineProviderForBaseBitmap dest(&target),s1(&first),s2(&second);
        tTVPDivisibleData data{}; data.Width=w;data.Height=h;data.Dest=&dest;data.Src1=&s1;data.Src2=&s2;
        if(mode==3) data.Src1=&dest;
        TVPLayerTransitionOperation op;op.params.kind=1;op.params.frameWidth=w;op.params.frameHeight=h;op.params.blockSize=2;
        const auto before=TVPGetMetalLayerRenderStats();
        krkrsdl3::layer_work::SetEnabled(diagnostics);
        TVPLayerTransitionResult result;
        {
            krkrsdl3::layer_work::TransitionScope scope("mosaic","mosaic",w,h,w,h,w,h,w,h,1,1);
            scope.SetPixels(w*h);result=TVPTryDivisibleMetalTransition(op,&data);
        }
        const auto after=TVPGetMetalLayerRenderStats();
        Require(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes &&
                after.cpuFallbacks==before.cpuFallbacks,"C4 resource preflight or dispatch crossed CPU boundary");
        const auto profile=krkrsdl3::layer_work::Take();
        if(diagnostics) Require(profile.transitionProfiles.find(mode>=3 ? "\"cpuCalls\":1" : "\"gpuCalls\":1")!=std::string::npos,
            "C4 production route not attributed");
        else Require(profile.transitionProfiles=="[]","disabled C4 diagnostics changed recording");
        krkrsdl3::layer_work::SetEnabled(false);
        if(mode>=3) {
            Require(result==(mode==3 ? TVPLayerTransitionResult::Alias : TVPLayerTransitionResult::CPUAccess),
                "C4 alias/lease rejection changed");
            if(lease) target.GetTexture()->UnlockCPU();
            Equal(target.GetTexture(),old);
        } else {
            Require(result==TVPLayerTransitionResult::Applied,"C4 bitmap route rejected");
            auto expected=old;
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) expected[y*w+x]=a[((y/2)*2+1)*w+(x/2)*2+1];
            Equal(target.GetTexture(),expected);
            if(snapshot) Equal(snapshot.get(),old);
            Equal(first.GetTexture(),a);Equal(second.GetTexture(),b);
        }
    }
    TestBitmap target(Create(old,w,h)),first(Create(a,w,h)),second(Create(b,w,h));
    auto* th=target.GetTexture()->GetTextureHandle();auto* sh1=first.GetTexture()->GetTextureHandle();auto* sh2=second.GetTexture()->GetTextureHandle();
    TVPLayerTransitionOperation invalid;invalid.params.frameWidth=w;invalid.params.frameHeight=h;
    invalid.params.width=w;invalid.params.height=h;invalid.params.blockSize=2;
    const auto before=TVPGetMetalLayerRenderStats();
    for(int kind:{-1,0,8,9,INT32_MAX}) {
        invalid.params.kind=kind;
        Require(TVPTryMetalLayerTransition(invalid,target.GetTexture(),first.GetTexture(),second.GetTexture())==TVPLayerTransitionResult::Unsupported,
            "C4 facade accepted invalid kind");
        Require(!backend->OperateLayerTransition(invalid,th,sh1,sh2) &&
                backend->LastLayerTransitionResult()==TVPLayerTransitionResult::Unsupported,
            "C4 backend encoded invalid kind");
    }
    const auto after=TVPGetMetalLayerRenderStats();
    Require(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
            "invalid C4 kind crossed CPU boundary");
    Equal(target.GetTexture(),old);
    std::cout<<"PASS C4 real bitmap COW, dirty overwrite, alias, lease, invalid kinds and diagnostic toggle\n";
}
void BitmapOverwriteTests(krkrsdl3::iTVPRenderBackend* backend) {
    constexpr int width=7,height=5;
    const auto oldPixels=Pixels(width,height,0x20406080u);
    const auto newPixels=Pixels(width,height,0xf0b09070u);
    // Exercise the real bitmap wrappers and GetTextureForRender COW path,
    // including their reference operand, instead of simulating a copied target.
    for(int conversion=0;conversion<3;++conversion) {
        krkrsdl3::layer_hotspot::Scope owner(900,"main");
        TestBitmap bitmap(Create(oldPixels,width,height));
        auto* original=bitmap.GetTexture(); original->GetTextureHandle(); original->AddRef();
        original->SetDiagnosticAsset("archive/streets/road.png");
        const auto originalIdentity=original->DiagnosticIdentity();
        Require(originalIdentity.texture && originalIdentity.session && originalIdentity.creatorLayer==900,
                "P2D stable texture/session/creator identity");
        TextureRef snapshot(original);
        auto expected=oldPixels;
        const tTVPRect rect(1,1,6,4);
        const tTVPGLGammaAdjustData data{1.7f,11,231,0.7f,2,247,2.3f,19,213};
        tTVPGLGammaAdjustTempData temp{}; TVPInitGammaAdjustTempData(&temp,&data);
        if(conversion<2) {
            for(int y=rect.top;y<rect.bottom;++y) {
                auto* row=expected.data()+size_t(y)*width+rect.left;
                if(conversion==0) TVPAdjustGamma(row,rect.get_width(),&temp);
                else TVPAdjustGamma_a(row,rect.get_width(),&temp);
            }
        } else TVPConvertAdditiveAlphaToAlpha(expected.data(),int(expected.size()));
        const auto before=TVPGetMetalLayerRenderStats();
        if(conversion==0) bitmap.AdjustGamma(rect,data);
        else if(conversion==1) bitmap.AdjustGammaForAdditiveAlpha(rect,data);
        else bitmap.ConvertAddAlphaToAlpha();
        const auto after=TVPGetMetalLayerRenderStats();
        const auto copiedIdentity=bitmap.GetTexture()->DiagnosticIdentity();
        Require(copiedIdentity.texture!=originalIdentity.texture && copiedIdentity.parent==originalIdentity.texture &&
                copiedIdentity.parentSession==originalIdentity.session && copiedIdentity.assetHash==originalIdentity.assetHash &&
                copiedIdentity.version>0,"P2D actual bitmap COW lineage/version lost");
        Require(bitmap.GetTexture()!=original && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "P1A bitmap COW conversion read back/uploaded/fell back");
        Equal(snapshot.get(),oldPixels); Equal(bitmap.GetTexture(),expected);
        TVPUninitGammaAdjustTempData(&temp);
    }
    // Native guards keep the original allocation alive without forcing COW
    // for a same-image read/write alias; real bitmap sharing still requires COW.
    for(bool shared:{false,true}) {
        TestBitmap bitmap(Create(oldPixels,width,height)); TestLayerCopy layer{&bitmap};
        auto* original=bitmap.GetTexture(); TextureRef snapshot;
        if(shared) {original->AddRef();snapshot.reset(original);}
        const auto before=TVPGetMetalLayerRenderStats();
        auto* target=layer.GetMainImageTextureForSpanComposite();
        const auto after=TVPGetMetalLayerRenderStats();
        Require((target==original)!=shared && !layer.ImageModified,
                "span target preparation changed COW or marked an uncommitted image modified");
        Require(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                "span COW target preparation acquired CPU pixels");
        Equal(target,oldPixels);if(shared) Equal(snapshot.get(),oldPixels);
    }
    for(bool shared:{false,true}) {
        TestBitmap bitmap(Create(oldPixels,width,height)); TestLayerCopy layer{&bitmap};
        auto* original=bitmap.GetTexture(); TextureRef snapshot;
        if(shared) { original->AddRef(); snapshot.reset(original); }
        tTVPScopedTexturePixels read,write;
        read.Acquire(layer.GetMainImageTextureForCPUAccess(false),false);
        auto* target=layer.GetMainImageTextureForCPUAccess(true);
        Require((target==original)!=shared,"native access changed bitmap COW semantics");
        write.Acquire(target,true);
        auto* data=static_cast<uint32_t*>(write.Data()); data[0]=0x12345678;
        write.Written(tTVPRect(0,0,1,1)); write.Reset(); read.Reset();
        auto expected=oldPixels; expected[0]=0x12345678;
        target->GetTextureHandle(); Equal(target,expected);
        if(shared) Equal(snapshot.get(),oldPixels);
    }
    TextureRef source(Create(newPixels,width,height));
    void* sourceHandle=source->GetTextureHandle();
    Require(sourceHandle,"overwrite source has no GPU handle");

    // Compile the production ROI transaction too: no preservation blit/read,
    // unchanged pixels outside the region, and no COW behind shared snapshots.
    const tTVPRect roi(2,1,5,4);
    {
        auto expected=oldPixels;
        for(int y=roi.top;y<roi.bottom;++y) for(int x=roi.left;x<roi.right;++x)
            expected[size_t(y)*width+x]=newPixels[size_t(y)*width+x];
        TestBitmap bitmap(Create(oldPixels,width,height));
        auto* original=bitmap.GetTexture();
        original->GetTextureHandle(); // Flush pending source CPU writes.
        const auto before=TVPGetMetalLayerRenderStats();
        TestLayerCopy layer{&bitmap};
        Require(layer.CopyMainImageFromGPUTargetRegion(backend,sourceHandle,width,height,roi),
                "exclusive Layer ROI copy rejected");
        Require(layer.ImageModified && bitmap.GetTexture()==original,"ROI copy changed identity or missed modified flag");
        NoPreservationWork(before);
        Equal(original,expected);
    }
    for(int rejection=0;rejection<5;++rejection) {
        TestBitmap bitmap(Create(oldPixels,width,height,rejection==1));
        auto* original=bitmap.GetTexture(); TextureRef snapshot;
        if(rejection==0) { original->AddRef(); snapshot.reset(original); }
        if(rejection==2) original->GetPersistentCPUData(false);
        if(rejection==3) original->GetPersistentCPUData(true);
        if(rejection==4) original->LockCPURead();
        TestLayerCopy layer{&bitmap};
        Require(!layer.CopyMainImageFromGPUTargetRegion(backend,sourceHandle,width,height,roi),
                "shared/static/leased ROI copy did preservation work instead of declining");
        Require(!layer.ImageModified && bitmap.GetTexture()==original,"failed ROI copy mutated Layer state");
        Equal(original,oldPixels);
        if(rejection==4) original->UnlockCPU();
        if(rejection==3) original->ReleasePersistentCPUData(nullptr);
    }
    {
        TestBitmap bitmap(Create(oldPixels,width,height)); TestLayerCopy layer{&bitmap};
        for(const auto& bad:{tTVPRect(-1,0,2,2),tTVPRect(0,0,width+1,2),tTVPRect(2,2,2,3)})
            Require(!layer.CopyMainImageFromGPUTargetRegion(backend,sourceHandle,width,height,bad),"invalid ROI accepted");
        Require(!layer.CopyMainImageFromGPUTargetRegion(backend,sourceHandle,width+1,height,roi),"ROI source size mismatch accepted");
        Require(!layer.CopyMainImageFromGPUTargetRegion(backend,nullptr,width,height,roi),"null ROI source accepted");
        Require(!layer.ImageModified,"rejected ROI attempts marked Layer modified");
        Equal(bitmap.GetTexture(),oldPixels);
    }

    // Exclusive mutable destination keeps its identity. Even an old CPU cache
    // must become stale after the copy, with no preserve-copy or upload first.
    {
        TestBitmap bitmap(Create(oldPixels,width,height));
        auto* original=bitmap.GetTexture();
        Equal(original,oldPixels);
        const auto before=TVPGetMetalLayerRenderStats();
        Require(bitmap.CopyFromGPUTarget(backend,sourceHandle),"exclusive overwrite rejected");
        Require(bitmap.GetTexture()==original,"exclusive overwrite replaced its texture");
        NoPreservationWork(before);
        Equal(bitmap.GetTexture(),newPixels);
    }
    // Shared and immutable destinations need a fresh texture. The snapshot
    // retains its original pixels while the bitmap adopts only the new result.
    for(bool immutable : {false,true}) {
        TestBitmap bitmap(Create(oldPixels,width,height,immutable));
        auto* original=bitmap.GetTexture();
        original->AddRef();
        TextureRef snapshot(original);
        const auto before=TVPGetMetalLayerRenderStats();
        Require(bitmap.CopyFromGPUTarget(backend,sourceHandle),"shared/static overwrite rejected");
        Require(bitmap.GetTexture()!=original,"shared/static overwrite mutated an alias");
        Require(bitmap.GetTexture()->IsIndependent(),"adopted overwrite texture leaked a reference");
        NoPreservationWork(before);
        Equal(snapshot.get(),oldPixels);
        Equal(bitmap.GetTexture(),newPixels);
    }
    // Static but otherwise exclusive is also copy-on-write.
    {
        TestBitmap bitmap(Create(oldPixels,width,height,true));
        auto* original=bitmap.GetTexture();
        const auto before=TVPGetMetalLayerRenderStats();
        Require(bitmap.CopyFromGPUTarget(backend,sourceHandle),"exclusive static overwrite rejected");
        Require(bitmap.GetTexture()!=original,"exclusive static texture overwritten in place");
        NoPreservationWork(before);
        Equal(bitmap.GetTexture(),newPixels);
    }
    // Rejection must leave identity, old contents, CPU cache and ownership
    // untouched, including after a candidate was allocated for a shared image.
    TextureRef wrongSize(Create(Pixels(width+1,height,0xdeadbeefu),width+1,height));
    void* wrongHandle=wrongSize->GetTextureHandle();
    for(bool shared : {false,true}) {
        TestBitmap bitmap(Create(oldPixels,width,height));
        auto* original=bitmap.GetTexture();
        TextureRef snapshot;
        if(shared) { original->AddRef(); snapshot.reset(original); }
        Equal(original,oldPixels);
        iTVPTexture2D::RecycleProcess();
        const auto before=TVPGetMetalLayerRenderStats();
        Require(!bitmap.CopyFromGPUTarget(backend,wrongHandle),"mismatched surface copy accepted");
        Require(bitmap.GetTexture()==original,"failed copy adopted an empty candidate");
        iTVPTexture2D::RecycleProcess();
        const auto after=TVPGetMetalLayerRenderStats();
        NoPreservationWork(before);
        Require(after.gpuResidentBytes==before.gpuResidentBytes,"rejected candidate leaked GPU texture");
        Require(after.cpuCacheBytes==before.cpuCacheBytes,"failed copy discarded old CPU cache");
        Equal(original,oldPixels);
        Require(!bitmap.CopyFromGPUTarget(backend,nullptr),"null source overwrite accepted");
        Require(!bitmap.CopyFromGPUTarget(nullptr,sourceHandle),"null backend overwrite accepted");
    }
    // Exposed CPU addresses and active read leases cannot be overwritten behind
    // their callers. The CPU fallback remains able to use exactly those pixels.
    for(int lease=0;lease<3;++lease) {
        TestBitmap bitmap(Create(oldPixels,width,height));
        auto* original=bitmap.GetTexture();
        const void* pointer=lease==0 ? original->GetPersistentCPUData(false) :
                            lease==1 ? original->GetPersistentCPUData(true) : original->LockCPURead();
        Require(pointer,"CPU lease returned no pixels");
        const auto before=TVPGetMetalLayerRenderStats();
        Require(!bitmap.CopyFromGPUTarget(backend,sourceHandle),"CPU-observable texture accepted GPU overwrite");
        Require(bitmap.GetTexture()==original,"CPU-observable texture replaced underneath pointer");
        NoPreservationWork(before);
        Require(!std::memcmp(pointer,oldPixels.data(),oldPixels.size()*4),"failed GPU overwrite changed leased CPU pixels");
        if(lease==2) original->UnlockCPU();
        const tTVPRect written(0,0,width,height);
        if(lease==1) original->ReleasePersistentCPUData(&written);
    }
    // The GPU shortcut covers the image allocation, not just the visible layer
    // rectangle. A rejected attempt cannot mark the layer as modified.
    {
        TestBitmap bitmap(Create(oldPixels,width,height));
        TestLayerCopy layer{&bitmap};
        Require(!layer.CopyMainImageFromGPUTarget(backend,sourceHandle,width-1,height),
                "GPU overwrite accepted dimensions smaller than image");
        Require(!layer.ImageModified,"rejected GPU overwrite marked image modified");
        Require(!layer.CopyMainImageFromGPUTarget(backend,sourceHandle,width,height+1),
                "GPU overwrite accepted dimensions larger than image");
        Equal(bitmap.GetTexture(),oldPixels);
        Require(layer.CopyMainImageFromGPUTarget(backend,sourceHandle,width,height),
                "exact image GPU overwrite rejected");
        Require(layer.ImageModified,"successful GPU overwrite did not mark image modified");
        Equal(bitmap.GetTexture(),newPixels);
    }
    // Validate a failed readback before acquiring a CPU overwrite lease: null
    // pixels, bad pitch and empty dimensions must not alter ownership or pixels.
    {
        TestBitmap bitmap(Create(oldPixels,width,height));
        TestLayerCopy layer{&bitmap};
        auto* original=bitmap.GetTexture(); original->AddRef(); TextureRef snapshot(original);
        const auto before=TVPGetMetalLayerRenderStats();
        Require(!layer.CopyMainImageFromCPU(nullptr,width*4,width,height),"null readback accepted");
        Require(!layer.CopyMainImageFromCPU(newPixels.data(),width*4-1,width,height),"short source pitch accepted");
        Require(!layer.CopyMainImageFromCPU(newPixels.data(),-1,width,height),"negative source pitch accepted");
        Require(!layer.CopyMainImageFromCPU(newPixels.data(),width*4,0,height),"empty source dimensions accepted");
        Require(!layer.ImageModified && bitmap.GetTexture()==original,
                "invalid CPU readback changed layer or texture ownership");
        NoPreservationWork(before);
        Equal(original,oldPixels);
    }
    // A canvas smaller than the image keeps pixels outside its rectangle. A
    // larger canvas clips safely. Padded source rows must use their actual stride.
    for(int sizeDelta : {-2,0,2}) {
        const int sourceWidth=width+sizeDelta, sourceHeight=height+sizeDelta;
        const int sourcePitchPixels=sourceWidth+3;
        auto input=Pixels(sourcePitchPixels,sourceHeight,0x79573513u);
        auto expected=oldPixels;
        for(int y=0;y<std::min(sourceHeight,height);++y)
            for(int x=0;x<std::min(sourceWidth,width);++x)
                expected[size_t(y)*width+x]=input[size_t(y)*sourcePitchPixels+x];
        TestBitmap bitmap(Create(oldPixels,width,height));
        TestLayerCopy layer{&bitmap};
        const auto before=TVPGetMetalLayerRenderStats();
        Require(layer.CopyMainImageFromCPU(input.data(),sourcePitchPixels*4,sourceWidth,sourceHeight),
                "valid strided CPU copy rejected");
        Require(layer.ImageModified,"CPU copy did not mark image modified");
        if(sizeDelta>=0)
            Require(TVPGetMetalLayerRenderStats().readbackBytes==before.readbackBytes,
                    "complete CPU overwrite unnecessarily fetched destination");
        Equal(bitmap.GetTexture(),expected);
    }
}

void TransitionOutputTests() {
    constexpr int width=7,height=5;
    const auto oldPixels=Pixels(width,height,0x20406080u),newPixels=Pixels(width,height,0xf0b09070u);
    TestBitmap first(Create(newPixels,width,height)),second(Create(oldPixels,width,height));
    tTVPScanLineProviderForBaseBitmap src1(&first),src2(&second);
    for(bool shared:{false,true}) {
        TestBitmap bitmap(Create(oldPixels,width,height));
        TextureRef snapshot;
        if(shared) { bitmap.GetTexture()->AddRef(); snapshot.reset(bitmap.GetTexture()); }
        tTVPScanLineProviderForBaseBitmap output(&bitmap);
        tTVPDivisibleData data{}; data.Dest=&output; data.Src1=&src1; data.Src2=&src2;
        data.Width=width; data.Height=height;
        for(int frame=0;frame<3;++frame) {
            const auto before=TVPGetMetalLayerRenderStats();
            {
                tTVPTransitionCPUOutputScope scope(data);
                for(int y=0;y<height;++y) {
                    void* row=nullptr; Require(TJS_SUCCEEDED(output.GetScanLineForWrite(y,&row)),"transition output unavailable");
                    std::memcpy(row,newPixels.data()+y*width,width*4);
                }
            }
            auto* texture=bitmap.GetTexture(); texture->GetTextureHandle();
            auto after=TVPGetMetalLayerRenderStats();
            Require(after.readbackBytes==before.readbackBytes,"full transition output fetched discarded target");
            Require(after.uploadedBytes==before.uploadedBytes+width*height*4,"transition output upload changed");
            texture->GetTextureHandle(); texture->GetTextureHandle();
            Require(TVPGetMetalLayerRenderStats().uploadedBytes==after.uploadedBytes,"transition output left an upload lease");
            Equal(texture,newPixels);
            // The next CPU frame follows a GPU write, invalidating its old CPU cache.
            auto* fill=TVPGetRenderManager()->GetRenderMethod("FillARGB"); fill->SetParameterColor4B(0,0x12345678);
            TVPGetRenderManager()->OperateRect(fill,texture,nullptr,tTVPRect(0,0,width,height),tRenderTexRectArray());
        }
        if(shared) Equal(snapshot.get(),oldPixels);
    }
    for(int mode=0;mode<3;++mode) {
        TestBitmap bitmap(Create(oldPixels,width,height));
        tTVPScanLineProviderForBaseBitmap output(&bitmap);
        tTVPDivisibleData data{}; data.Dest=&output; data.Src1=mode==1 ? &output : &src1;
        data.Width=mode==0 ? width-2 : width; data.Height=height;
        if(mode==0) data.DestLeft=1;
        uint32_t* raw=mode==2 ? static_cast<uint32_t*>(bitmap.GetTexture()->GetPersistentCPUData(true)) : nullptr;
        const auto before=TVPGetMetalLayerRenderStats();
        {
            tTVPTransitionCPUOutputScope scope(data);
            for(int y=0;y<height;++y) {
                void* row=nullptr; output.GetScanLineForWrite(y,&row);
                std::memcpy(static_cast<uint32_t*>(row)+data.DestLeft,newPixels.data()+y*width+data.DestLeft,data.Width*4);
            }
        }
        auto expected=oldPixels;
        for(int y=0;y<height;++y) for(int x=data.DestLeft;x<data.DestLeft+data.Width;++x) expected[y*width+x]=newPixels[y*width+x];
        if(mode<2) Require(TVPGetMetalLayerRenderStats().readbackBytes==before.readbackBytes+width*height*4,
                           "partial/aliased transition discarded pixels");
        else Require(raw==bitmap.GetTexture()->GetPersistentCPUData(true) && !std::memcmp(raw,expected.data(),expected.size()*4),
                     "transition invalidated raw script pointer");
        bitmap.GetTexture()->GetTextureHandle(); Equal(bitmap.GetTexture(),expected);
        if(mode==2) bitmap.GetTexture()->ReleasePersistentCPUData(nullptr);
    }
    // A GPU transition never requests scanlines and must leave no CPU state.
    {
        TestBitmap bitmap(Create(oldPixels,width,height)); tTVPScanLineProviderForBaseBitmap output(&bitmap);
        tTVPDivisibleData data{}; data.Dest=&output; data.Src1=&src1; data.Width=width; data.Height=height;
        const auto before=TVPGetMetalLayerRenderStats();
        { tTVPTransitionCPUOutputScope scope(data); output.GetTextureForRender()->GetTextureHandle(); }
        NoPreservationWork(before);
    }
    // Unwind a partial write; its access lease must end even when the handler throws.
    {
        TestBitmap bitmap(Create(oldPixels,width,height)); tTVPScanLineProviderForBaseBitmap output(&bitmap);
        tTVPDivisibleData data{}; data.Dest=&output; data.Src1=&src1; data.Width=1; data.Height=1;
        try { tTVPTransitionCPUOutputScope scope(data); void* row=nullptr; output.GetScanLineForWrite(0,&row);
            *static_cast<uint32_t*>(row)=newPixels[0]; throw std::runtime_error("handler failure"); }
        catch(const std::runtime_error&) {}
        auto* texture=bitmap.GetTexture(); texture->GetTextureHandle(); const auto uploaded=TVPGetMetalLayerRenderStats().uploadedBytes;
        texture->GetTextureHandle(); Require(TVPGetMetalLayerRenderStats().uploadedBytes==uploaded,"failed transition leaked write lease");
        auto expected=oldPixels; expected[0]=newPixels[0]; Equal(texture,expected);
    }
}

void BitmapRenderSessionCacheTests(krkrsdl3::iTVPRenderBackend* backend) {
    constexpr int w=48,h=40;
    // First use with no live Layer/Bitmap: the returned default must own its
    // texture after the temporary holder itself has been destroyed.
    Require(!TVPTempBitmapHolder,"cache fixture unexpectedly retained a holder");
    for(int session=0;session<3;++session) {
        Require(TVPBindMetalLayerRenderManager(backend),"standalone prototype session failed");
        {
            tTVPBaseTexture initial(TVPGetInitialBitmap());
            Require(!TVPTempBitmapHolder,"standalone prototype leaked its holder");
            Require(initial.GetWidth()==32 && initial.GetHeight()==32 &&
                    initial.GetTexture()->GetPoint(0,0)==0x00ffffff,
                    "standalone default bitmap lost ownership");
            TVPUnbindMetalLayerRenderManager();
            Require(initial.GetTexture()->IsCPUResident() && initial.GetTexture()->GetPoint(0,0)==0x00ffffff,
                    "standalone prototype did not survive session detach");
        }
        iTVPTexture2D::RecycleProcess();
    }
    tTVPTempBitmapHolder::AddRef(); // Retain the real holder across A -> B -> A.
    uint64_t lastInitial=0,lastGlyph=0,lastTemp=0;
    TextureRef retainedPrototype;
    for(int session=0;session<4;++session) {
        Require(TVPBindMetalLayerRenderManager(backend),"cache regression could not bind session");
        auto* manager=TVPGetRenderManager();
        auto* prototype=tTVPTempBitmapHolder::Get()->GetTexture();
        if(session==0) { prototype->AddRef(); retainedPrototype.reset(prototype); }
        else Require(retainedPrototype->IsCPUResident() && retainedPrototype->GetPoint(0,0)==0x00ffffff,
                     "cache regeneration damaged a retained old Layer snapshot");
        uint64_t identity=0,version=0;
        Require(prototype->GetContentKey(identity,version) && identity!=lastInitial,
                "initial Layer prototype retained an old render session");
        lastInitial=identity;
        const auto beforeCopy=TVPGetMetalLayerRenderStats();
        TextureRef clone(manager->CreateTexture2D(w,h,prototype));
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==beforeCopy.cpuFallbacks &&
                TVPGetMetalLayerRenderStats().readbackBytes==beforeCopy.readbackBytes,
                "initial Layer clone fell back after switching game");
        auto* temporary=tTVPTempBitmapHolder::GetTemp(24,20,true);
        Require(std::find(testCompactHooks.begin(),testCompactHooks.end(),TVPTempBitmapHolder)!=testCompactHooks.end(),
                "retained temporary cache did not register compaction for the next session");
        Require(temporary->GetTexture()->GetContentKey(identity,version) && identity!=lastTemp,
                "temporary Layer bitmap retained an old render session");
        lastTemp=identity; tTVPTempBitmapHolder::FreeTemp();

        // A smaller second-session glyph must trigger ownership replacement,
        // even though the old scratch texture is still large enough.
        const int gw=session==0 ? 16 : 8,gh=session==0 ? 12 : 4;
        std::vector<uint8_t> alpha(size_t(gw)*gh);
        for(size_t i=0;i<alpha.size();++i) alpha[i]=uint8_t(i%65);
        tGlyphMetrics metrics{};
        tTVPCharacterData character(alpha.data(),gw,0,0,gw,gh,metrics,false);
        TestBitmap bitmap(Create(Pixels(w,h,0x31415926),w,h));
        tTVPDrawTextData draw{tTVPRect(0,0,w,h),w*4,255,true,bmAlphaOnAlpha};
        tTVPRect src(0,0,gw,gh),dst(2,3,2+gw,3+gh);
        const auto beforeGlyph=TVPGetMetalLayerRenderStats();
        for(int glyph=0;glyph<128;++glyph)
            Require(bitmap.BlendGlyph(&character,&draw,0xff3579bd,src,dst),"production glyph draw rejected");
        const auto afterGlyph=TVPGetMetalLayerRenderStats();
        if(afterGlyph.cpuFallbacks!=beforeGlyph.cpuFallbacks)
            std::cerr<<"cache session="<<session<<" glyph fallback="<<afterGlyph.cpuFallbacks-beforeGlyph.cpuFallbacks
                     <<" readbackBytes="<<afterGlyph.readbackBytes-beforeGlyph.readbackBytes<<'\n';
        Require(afterGlyph.gpuOperations==beforeGlyph.gpuOperations+128 &&
                afterGlyph.cpuFallbacks==beforeGlyph.cpuFallbacks && afterGlyph.readbackBytes==beforeGlyph.readbackBytes,
                "glyph cache forced CPU fallback/readback in the next game");
        if(_CharacterTexture) {
            Require(!manager->CanReuseCachedTexture(_CharacterTexture) ||
                    (_CharacterTexture->GetContentKey(identity,version) && identity!=lastGlyph),
                    "glyph scratch retained an old render session");
            if(manager->CanReuseCachedTexture(_CharacterTexture)) lastGlyph=identity;
        }

        // Compare the sequence with the actual software operator, including
        // glyph alpha, repeated blends, clipping and existing target pixels.
        auto* sw=TVPGetSoftwareRenderManager();
        auto initial=Pixels(w,h,0x31415926);
        TextureRef expected(sw->CreateTexture2D(initial.data(),w*4,w,h,TVPTextureFormat::RGBA));
        TextureRef gray(sw->CreateTexture2D(character.GetData(),character.Pitch,gw,gh,TVPTextureFormat::Gray));
        auto* method=manager->GetRenderMethod("ApplyColorMap_d");
        method->SetParameterOpa(method->EnumParameterID("opacity"),255);
        method->SetParameterColor4B(method->EnumParameterID("color"),0xff3579bd);
        tRenderTexRectArray::Element input(gray.get(),src);
        for(int glyph=0;glyph<128;++glyph) sw->OperateRect(method,expected.get(),nullptr,dst,tRenderTexRectArray(&input,1));
        const auto* expectedPixels=static_cast<const uint32_t*>(expected->GetScanLineForRead(0));
        std::vector<uint32_t> pixels(expectedPixels,expectedPixels+w*h); Equal(bitmap.GetTexture(),pixels);
        // Intentionally retain process caches and snapshots through unbinding.
        // The next session must regenerate caches, while old snapshots stay safe.
        TVPUnbindMetalLayerRenderManager();
        testCompactHooks.clear(); // Model removal of nonpersistent hooks at game exit.
        Require(prototype->IsCPUResident(),"old prototype was not detached safely");
        if(session==1) {
            TestBitmap cpu(TVPGetSoftwareRenderManager()->CreateTexture2D(nullptr,0,w,h,TVPTextureFormat::RGBA));
            Require(cpu.BlendGlyph(&character,&draw,0xff3579bd,src,dst),"software-between-games glyph failed");
        }
    }
    if(_CharacterTexture) {_CharacterTexture->Release(); _CharacterTexture=nullptr;}
    tTVPTempBitmapHolder::Release();
    retainedPrototype.reset();
    iTVPTexture2D::RecycleProcess();
}

namespace {
void LegacyGlyphReference(std::vector<uint32_t>& target,int width,int height,
                          const tTVPCharacterData& glyph,const tTVPDrawTextData& draw,
                          uint32_t color,const tTVPRect& source,const tTVPRect& destination) {
    auto* software=TVPGetSoftwareRenderManager();
    TextureRef output(software->CreateTexture2D(target.data(),width*4,width,height,TVPTextureFormat::RGBA));
    // The old caller crops rows and dimensions, but ignores source.left. This
    // oracle deliberately freezes that existing byte interpretation for C3.
    TextureRef mask(software->CreateTexture2D(glyph.GetData()+source.top*glyph.Pitch,glyph.Pitch,
        destination.get_width(),destination.get_height(),TVPTextureFormat::Gray));
    const char* name=draw.bltmode==bmAlphaOnAlpha ? (draw.opa>0 ? "ApplyColorMap_d" : "RemoveOpacity") :
                     draw.bltmode==bmAlphaOnAddAlpha ? "ApplyColorMap_a" : "ApplyColorMap";
    auto* method=software->GetRenderMethod(name);
    method->SetParameterOpa(method->EnumParameterID("opacity"),draw.opa);
    method->SetParameterColor4B(method->EnumParameterID("color"),color);
    tRenderTexRectArray::Element input(mask.get(),tTVPRect(0,0,destination.get_width(),destination.get_height()));
    software->OperateRect(method,output.get(),nullptr,destination,tRenderTexRectArray(&input,1));
}
}

void GlyphCallerTests(krkrsdl3::iTVPRenderBackend* backend,const std::function<void(int)>& failure) {
    constexpr int width=13,height=11,gw=7,gh=5;
    auto initial=Pixels(width,height,0x31415926);
    std::vector<uint8_t> input(size_t(gw)*gh);
    for(size_t i=0;i<input.size();++i) input[i]=uint8_t(i*7+3);
    tGlyphMetrics metrics{};
    tTVPCharacterData glyph(input.data(),gw,0,0,gw,gh,metrics,false);
    auto* manager=TVPGetRenderManager();

    // Exercise both production text functions, rather than drawing a reused,
    // already-uploaded test mask through the generic operator directly.
    for(bool diagnostics:{false,true}) for(auto mode:{bmAlpha,bmAlphaOnAlpha,bmAlphaOnAddAlpha}) {
        TestBitmap bitmap(Create(initial,width,height));
        auto expected=initial;
        tTVPDrawTextData draw{tTVPRect(3,3,10,8),width*4,191,false,mode};
        const tTVPRect expectedDestination(3,3,8,7),source(2,1,7,5);
        krkrsdl3::layer_work::SetEnabled(diagnostics);
        const auto before=TVPGetMetalLayerRenderStats();
        for(int pass=0;pass<4;++pass) {
            // Reuse exactly the same CharacterData address while changing every
            // byte, alpha and color. No content identity may suppress a copy.
            for(int y=0;y<gh;++y) for(int x=0;x<gw;++x)
                glyph.GetData()[y*glyph.Pitch+x]=uint8_t(pass*41+x*19+y*7);
            draw.holdalpha=pass%2!=0;
            const uint32_t color=0x9153a7e1u+uint32_t(pass)*0x03110709u;
            tTVPRect destination;
            Require(bitmap.DrawGlyphData(&glyph,1,2,color,&draw,destination),"production clipped glyph rejected");
            Require(destination==expectedDestination,"production glyph clipping rectangle changed");
            LegacyGlyphReference(expected,width,height,glyph,draw,color,source,destination);
        }
        const auto after=TVPGetMetalLayerRenderStats();
        Require(bitmap.targetAcquisitions==4,"glyph acquired its COW target more than once");
        Require(after.gpuOperations==before.gpuOperations+4 && after.cpuFallbacks==before.cpuFallbacks &&
                after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes+80,
                "glyph snapshot caller changed route, logical bytes or target residency");
        const auto profile=krkrsdl3::layer_work::Take();
        Require(diagnostics ? profile.transferOrigins.find("upload:bitmap.update=4/80/")!=std::string::npos :
                              profile.transferOrigins.empty(),"glyph snapshot C0 attribution was lost");
        krkrsdl3::layer_work::SetEnabled(false);
        try {Equal(bitmap.GetTexture(),expected);}
        catch(...) {std::cerr<<"C3 changing bytes diagnostics="<<diagnostics<<" mode="<<int(mode)<<'\n';throw;}
        const auto acquired=bitmap.targetAcquisitions;
        draw.rect=tTVPRect(10,9,12,10);tTVPRect invisible;
        Require(!bitmap.DrawGlyphData(&glyph,0,0,0xff123456,&draw,invisible) &&
                bitmap.targetAcquisitions==acquired,"fully clipped glyph acquired or changed target");
    }

    // A shared image must preserve the previous Layer snapshot. Ordinary target
    // acquisition performs the production COW transaction before either route.
    {
        TestBitmap bitmap(Create(initial,width,height));
        auto* original=bitmap.GetTexture();original->AddRef();TextureRef snapshot(original);
        tTVPDrawTextData draw{tTVPRect(0,0,width,height),width*4,255,true,bmAlphaOnAlpha};
        tTVPRect src(0,0,gw,gh),dst(2,3,2+gw,3+gh);
        auto expected=initial;
        Require(bitmap.BlendGlyph(&glyph,&draw,0x713579bd,src,dst),"COW glyph rejected");
        LegacyGlyphReference(expected,width,height,glyph,draw,0x713579bd,src,dst);
        Require(bitmap.GetTexture()!=original && bitmap.targetAcquisitions==1,"glyph bypassed or repeated target COW");
        Equal(original,initial);Equal(bitmap.GetTexture(),expected);
    }

    // Negative opacity retains RemoveOpacity's software behavior. CPU write
    // leases and software targets likewise retain the ordinary scratch route.
    for(int route=0;route<3;++route) {
        auto* texture=route==2 ? TVPGetSoftwareRenderManager()->CreateTexture2D(nullptr,0,width,height,TVPTextureFormat::RGBA) :
                               Create(initial,width,height);
        if(route==2) texture->Update(initial.data(),TVPTextureFormat::RGBA,width*4,tTVPRect(0,0,width,height));
        TestBitmap bitmap(texture);
        tTVPDrawTextData draw{tTVPRect(0,0,width,height),width*4,route==0 ? -127 : 191,true,bmAlphaOnAlpha};
        tTVPRect src(0,0,gw,gh),dst(2,3,2+gw,3+gh);
        auto expected=initial;
        if(route==1) Require(texture->LockCPUWrite()!=nullptr,"glyph CPU lease failed");
        const auto before=TVPGetMetalLayerRenderStats();
        Require(bitmap.BlendGlyph(&glyph,&draw,0x713579bd,src,dst),"ordinary glyph fallback rejected");
        Require(TVPGetMetalLayerRenderStats().cpuFallbacks==before.cpuFallbacks+1,
                "glyph preflight counted fallback twice or bypassed the CPU boundary");
        LegacyGlyphReference(expected,width,height,glyph,draw,0x713579bd,src,dst);
        if(route==1) texture->UnlockCPUWrite(dst);
        Equal(bitmap.GetTexture(),expected);
        Require(bitmap.targetAcquisitions==1,"fallback glyph reacquired target");
    }

    if(failure) {
        for(int stage:{0,1,2}) {
            TestBitmap bitmap(Create(initial,width,height));
            tTVPDrawTextData draw{tTVPRect(0,0,width,height),width*4,191,true,bmAlphaOnAlpha};
            tTVPRect src(0,0,gw,gh),dst(2,3,2+gw,3+gh);
            auto expected=initial;
            // Prime the real CPU cache: a post-dispatch exception must not leave
            // it current, and must never replay a partly encoded glyph.
            Equal(bitmap.GetTexture(),initial);
            const auto before=TVPGetMetalLayerRenderStats();
            krkrsdl3::layer_work::SetEnabled(true);
            failure(stage);bool thrown=false;
            try {bitmap.BlendGlyph(&glyph,&draw,0x713579bd,src,dst);}
            catch(const std::bad_alloc&) {thrown=true;}
            failure(-1);
            Require(thrown==(stage==1),"glyph exception was swallowed or unsupported route threw");
            LegacyGlyphReference(expected,width,height,glyph,draw,0x713579bd,src,dst);
            Require(bitmap.targetAcquisitions==1,"failed glyph reacquired target");
            const auto after=TVPGetMetalLayerRenderStats();
            Require(after.gpuOperations==before.gpuOperations+1 && after.cpuFallbacks==before.cpuFallbacks &&
                    after.uploadedBytes==before.uploadedBytes+gw*gh,
                    "glyph optimization rejection/exception lost or duplicated committed work");
            const auto profile=krkrsdl3::layer_work::Take();
            if(stage==2) Require(profile.transferOrigins.empty(),"glyph upload crossed a diagnostics generation");
            else Require(profile.transferOrigins.find("upload:bitmap.update=1/35/")!=std::string::npos,
                         "glyph failure/rejection did not record exactly one committed upload");
            krkrsdl3::layer_work::SetEnabled(false);
            Equal(bitmap.GetTexture(),expected);
        }
    }
    // The optional API validates raw input and active leases before touching an
    // atlas or destination. These declines must not record ordinary GPU rejects.
    {
        TextureRef target(Create(initial,width,height));
        auto* method=manager->GetRenderMethod("ApplyColorMap_d");
        method->SetParameterOpa(method->EnumParameterID("opacity"),191);
        method->SetParameterColor4B(method->EnumParameterID("color"),0x713579bd);
        const tTVPRect dst(2,3,2+gw,3+gh);
        const auto before=TVPGetMetalLayerRenderStats();
        Require(!manager->TryBlendGlyph(method,target.get(),dst,nullptr,glyph.Pitch,gw,gh) &&
                !manager->TryBlendGlyph(method,target.get(),dst,glyph.GetData(),gw-1,gw,gh),"invalid glyph input accepted");
        Require(target->LockCPURead()!=nullptr,"glyph read lease failed");
        Require(!manager->TryBlendGlyph(method,target.get(),dst,glyph.GetData(),glyph.Pitch,gw,gh),"glyph accepted active CPU read lease");
        target->UnlockCPU();
        const auto after=TVPGetMetalLayerRenderStats();
        for(int i=0;i<int(TVPLayerGPURejectReason::Count);++i)
            Require(after.gpuRejectCountByReason[i]==before.gpuRejectCountByReason[i],"optional glyph decline changed ordinary GPU rejects");
        Equal(target.get(),initial);
        krkrsdl3::layer_work::SetEnabled(true);
        const auto beforeNoOp=TVPGetMetalLayerRenderStats();
        Require(manager->TryBlendGlyph(method,target.get(),tTVPRect(20,20,20+gw,20+gh),
                    glyph.GetData(),glyph.Pitch,gw,gh),"fully clipped valid glyph declined");
        const auto afterNoOp=TVPGetMetalLayerRenderStats();
        Require(afterNoOp.uploadedBytes==beforeNoOp.uploadedBytes &&
                krkrsdl3::layer_work::Take().transferOrigins.empty(),"glyph no-op recorded an upload");
        krkrsdl3::layer_work::SetEnabled(false);
        Equal(target.get(),initial);
    }
    if(_CharacterTexture) {_CharacterTexture->Release();_CharacterTexture=nullptr;}
    std::cout<<"PASS C3 production glyph caller: changing bytes, legacy clipping/HDA, COW, leases, fallback, errors and C0 epochs\n";
    (void)backend;
}
