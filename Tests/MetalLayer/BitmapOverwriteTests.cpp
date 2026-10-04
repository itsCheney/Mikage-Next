#include "tjsCommHead.h"
#include "LayerBitmap.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "TVPCompositor.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

// Only font/platform setup is stubbed. The transaction and texture adoption
// below are extracted from production and use the real bitmap declaration,
// reference counts, GPU textures, CPU caches, and render manager.
tTVPNativeBaseBitmap::tTVPNativeBaseBitmap()
    : Font{}, FontChanged(true), GlobalFontState(-1), PrerenderedFont(nullptr),
      Bitmap(nullptr) {}
tTVPNativeBaseBitmap::~tTVPNativeBaseBitmap() {
    if(Bitmap) Bitmap->Release();
}
// Window/script event machinery is outside this test. The layer facade keeps
// only the two fields used by the exact production copy methods below.
class TestLayerCopy {
public:
    tTVPNativeBaseBitmap* MainImage;
    bool ImageModified=false;
    iTVPTexture2D* GetMainImageTextureForCPUAccess(bool);
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
class TestBitmap final : public tTVPNativeBaseBitmap {
public:
    explicit TestBitmap(iTVPTexture2D* texture) { Bitmap=texture; }
    iTVPRenderManager* GetRenderManager() override { return TVPGetRenderManager(); }
};
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
    const auto* actual=static_cast<const uint32_t*>(texture->GetScanLineForRead(0));
    Require(actual && !std::memcmp(actual,expected.data(),expected.size()*4),
            "bitmap overwrite produced wrong pixels");
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

void BitmapOverwriteTests(krkrsdl3::iTVPRenderBackend* backend) {
    constexpr int width=7,height=5;
    const auto oldPixels=Pixels(width,height,0x20406080u);
    const auto newPixels=Pixels(width,height,0xf0b09070u);
    // Native guards keep the original allocation alive without forcing COW
    // for a same-image read/write alias; real bitmap sharing still requires COW.
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
