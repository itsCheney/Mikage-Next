#include "tjsCommHead.h"
#include "tjsNative.h"
#include "RenderManager.h"
#include "CPUConsumerTrace.h"
#include "PointReadTrace.h"
#include "LayerBitmap.h"
#include "LayerShrinkGeometry.h"
#include "MetalLayerRenderManager.h"
#include "TVPCompositor.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <functional>
#include <memory>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void RequireShrink(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct TestShrinkClass { static constexpr tjs_int32 ClassID=0x123456; };
class TestShrinkBitmap : public iTVPBaseBitmap {
    iTVPRenderManager* manager;
public:
    TestShrinkBitmap(iTVPTexture2D* texture,iTVPRenderManager* renderer):manager(renderer) { Bitmap=texture; }
    iTVPRenderManager* GetRenderManager() override { return manager; }
};
class TestShrinkNativeLayer : public tTJSNativeInstance {
public:
    std::unique_ptr<TestShrinkBitmap> image;
    unsigned resizeCalls=0,customGets=0;
    explicit TestShrinkNativeLayer(TestShrinkBitmap* bitmap):image(bitmap) {}
    TestShrinkBitmap* GetMainImage() { return image.get(); }
    iTVPTexture2D* GetMainImageTextureForCPUAccess(bool write) {
        return write ? image->GetTextureForRender(true,nullptr) : image->GetTexture();
    }
};
#include "ProductionShrinkCopy.inc"

TestShrinkNativeLayer* Layer(iTJSDispatch2* object) {
    TestShrinkNativeLayer* layer=nullptr;
    RequireShrink(TJS_SUCCEEDED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,TestShrinkClass::ClassID,
        reinterpret_cast<iTJSNativeInstance**>(&layer))) && layer,"fixture layer not native");
    return layer;
}
tjs_error HasImage(tTJSVariant* result,iTJSDispatch2*) { *result=1;return TJS_S_OK; }
tjs_error CustomHasImage(tTJSVariant* result,iTJSDispatch2* object) {
    ++Layer(object)->customGets; *result=1;return TJS_S_OK;
}
tjs_error Width(tTJSVariant* result,iTJSDispatch2* object) { *result=tjs_int(Layer(object)->image->GetWidth());return TJS_S_OK; }
tjs_error Height(tTJSVariant* result,iTJSDispatch2* object) { *result=tjs_int(Layer(object)->image->GetHeight());return TJS_S_OK; }
tjs_error Pitch(tTJSVariant* result,iTJSDispatch2* object) { *result=Layer(object)->image->GetTexture()->GetPitch();return TJS_S_OK; }
tjs_error Deny(const tTJSVariant*,iTJSDispatch2*) { return TJS_E_ACCESSDENYED; }
tjs_error Resize(tTJSVariant*,tjs_int count,tTJSVariant** parameters,iTJSDispatch2* object) {
    if(count<2) return TJS_E_BADPARAMCOUNT;
    auto* layer=Layer(object);++layer->resizeCalls;
    layer->image->SetSize(tjs_uint(parameters[0]->AsInteger()),tjs_uint(parameters[1]->AsInteger()));
    return TJS_S_OK;
}
tjs_error CustomResize(tTJSVariant* result,tjs_int count,tTJSVariant** parameters,iTJSDispatch2* object) {
    return Resize(result,count,parameters,object);
}
void Property(iTJSDispatch2* object,const tjs_char* name,tTJSNativeClassPropertyGetCallback callback,
              iTJSDispatch2* binding=nullptr) {
    auto* property=TJSCreateNativeClassProperty(callback,Deny);
    tTJSVariant value(property,binding ? binding : object);
    RequireShrink(TJS_SUCCEEDED(object->PropSet(TJS_MEMBERENSURE|TJS_IGNOREPROP,name,nullptr,&value,object)),"fixture property failed");
    property->Release();
}
void Method(iTJSDispatch2* object,tTJSNativeClassMethodCallback callback) {
    auto* method=TJSCreateNativeClassMethod(callback);
    tTJSVariant value(method,object);
    object->PropSet(TJS_MEMBERENSURE|TJS_IGNOREPROP,TJS_N("setImageSize"),nullptr,&value,object);
    method->Release();
}
struct ObjectRelease { void operator()(iTJSDispatch2* object) const { if(object) {object->Invalidate(0,nullptr,nullptr,object);object->Release();} } };
using ObjectRef=std::unique_ptr<iTJSDispatch2,ObjectRelease>;
std::vector<uint32_t> Image(int w,int h,int seed) {
    std::vector<uint32_t> pixels(size_t(w)*h);
    for(size_t n=0;n<pixels.size();++n) pixels[n]=uint32_t(seed)+uint32_t(n)*0x37051379u;
    return pixels;
}
ObjectRef Object(iTVPRenderManager* manager,int w,int h,const std::vector<uint32_t>& pixels,bool custom=false) {
    auto* texture=manager->CreateTexture2D(nullptr,0,w,h,TVPTextureFormat::RGBA);
    texture->Update(pixels.data(),TVPTextureFormat::RGBA,w*4,tTVPRect(0,0,w,h));
    auto* object=new tTJSCustomObject;
    iTJSNativeInstance* layer=new TestShrinkNativeLayer(new TestShrinkBitmap(texture,manager));
    object->NativeInstanceSupport(TJS_NIS_REGISTER,TestShrinkClass::ClassID,&layer);
    tTJSVariant name(TJS_N("Layer"));object->ClassInstanceInfo(TJS_CII_ADD,0,&name);
    Property(object,TJS_N("hasImage"),custom ? CustomHasImage : HasImage);
    Property(object,TJS_N("imageWidth"),Width);Property(object,TJS_N("imageHeight"),Height);
    Property(object,TJS_N("mainImageBufferPitch"),Pitch);Method(object,Resize);
    return ObjectRef(object);
}
std::vector<uint32_t> Read(iTJSDispatch2* object) {
    auto* texture=Layer(object)->image->GetTexture();
    tTVPScopedTexturePixels access;access.Acquire(texture,false,"test.shrink.result");
    RequireShrink(access.Data()!=nullptr,"missing shrink result");
    const int w=int(texture->GetWidth()),h=int(texture->GetHeight());
    std::vector<uint32_t> result(size_t(w)*h);
    for(int y=0;y<h;++y) std::memcpy(result.data()+size_t(y)*w,
        static_cast<const uint8_t*>(access.Data())+size_t(y)*access.Pitch(),size_t(w)*4);
    return result;
}
template<typename Avg=unsigned long>
tjs_error Area(iTJSDispatch2* destination,iTJSDispatch2* source,double dx,double dy,double dw,double dh,
               int sx,int sy,int sw,int sh) {
    tTJSVariant args[]={tTJSVariant(dx),tTJSVariant(dy),tTJSVariant(dw),tTJSVariant(dh),tTJSVariant(source,source),
        tTJSVariant(sx),tTJSVariant(sy),tTJSVariant(sw),tTJSVariant(sh)};
    tTJSVariant* parameters[9];for(int i=0;i<9;++i) parameters[i]=&args[i];
    return ShrinkCopy<Avg>::layerShrinkCopy(nullptr,9,parameters,destination);
}
tjs_error Fast(iTJSDispatch2* destination,iTJSDispatch2* source,int x,int y) {
    tTJSVariant args[]={tTJSVariant(source,source),tTJSVariant(x),tTJSVariant(y)};
    tTJSVariant* parameters[]={&args[0],&args[1],&args[2]};
    return LimitedShrink::layerShrinkCopy(nullptr,3,parameters,destination);
}
void Same(iTJSDispatch2* expected,iTJSDispatch2* actual,const char* message) {
    RequireShrink(Read(expected)==Read(actual),message);
}
void AliasProofTests() {
    uint32_t state=0x29173;
    const auto next=[&]() {state=state*1664525u+1013904223u;return state;};
    for(unsigned n=0;n<2048;++n) {
        TVPLayerShrinkOperation operation;operation.kind=TVPLayerShrinkKind::Area;
        const int left=int(next()%4),top=int(next()%4),w=1+int(next()%5),h=1+int(next()%5);
        operation.destination={left,top,left+w,top+h};
        auto x=std::make_shared<std::vector<TVPLayerShrinkAxis>>();
        auto y=std::make_shared<std::vector<TVPLayerShrinkAxis>>();
        for(int i=0;i<w;++i) x->push_back({int32_t(1+next()%12),int32_t(next()%5)-1,next()%2,1,next()%2,1,256});
        for(int i=0;i<h;++i) y->push_back({int32_t(1+next()%12),int32_t(next()%5)-1,next()%2,1,next()%2,1,256});
        operation.horizontal=x;operation.vertical=y;
        bool bruteSafe=true;
        for(int row=0;row<h;++row) for(int col=0;col<w;++col) {
            const auto xs=TVPLayerShrinkGeometry::ReadSpan((*x)[size_t(col)],operation.kind);
            const auto ys=TVPLayerShrinkGeometry::ReadSpan((*y)[size_t(row)],operation.kind);
            for(int64_t ry=ys.first;ry<=ys.last;++ry) for(int64_t rx=xs.first;rx<=xs.last;++rx)
                if(rx>=left && rx<left+w && ry>=top &&
                   (ry<top+row || (ry==top+row && rx<left+col))) bruteSafe=false;
        }
        RequireShrink(TVPLayerShrinkGeometry::AliasSafe(operation)==bruteSafe,
                      "factored alias proof differs from actual CPU write dependency");
    }
}
void ArithmeticValidationTests() {
    TVPLayerShrinkOperation operation;operation.kind=TVPLayerShrinkKind::Area;operation.avgBits=64;
    operation.destination={0,0,1,1};
    auto x=std::make_shared<std::vector<TVPLayerShrinkAxis>>();
    auto y=std::make_shared<std::vector<TVPLayerShrinkAxis>>();
    x->push_back({1,-1,128,128,128,128,256});
    y->push_back({1,-1,128,128,128,128,256});
    operation.horizontal=x;operation.vertical=y;
    TVPLayerShrinkGeometry::Validation validation;
    RequireShrink(TVPLayerShrinkGeometry::Validate(operation,1,1,1,1,false,validation)==TVPLayerShrinkResult::Applied &&
        validation.safe32 && validation.sourceRows==1,"repeated endpoint validation failed");
    (*x)[0].ta=0;(*x)[0].ba=0;(*x)[0].base=-100;
    RequireShrink(TVPLayerShrinkGeometry::Validate(operation,1,1,1,1,false,validation)==TVPLayerShrinkResult::Applied,
                  "zero alpha gate invented an out-of-bounds read");
    (*x)[0]={1,-1,256,511,0,0,65536};(*y)[0]={1,-1,256,511,0,0,65536};
    operation.avgBits=32;
    RequireShrink(TVPLayerShrinkGeometry::Validate(operation,1,1,1,1,false,validation)==TVPLayerShrinkResult::Arithmetic,
                  "32-bit wrapped zero denominator accepted");
    operation.avgBits=64;
    RequireShrink(TVPLayerShrinkGeometry::Validate(operation,1,1,1,1,false,validation)==TVPLayerShrinkResult::Applied &&
        !validation.safe32 && !TVPLayerShrinkGeometry::CanUse32(operation),"wide denominator silently narrowed");
    (*x)[0].total=1;(*y)[0].total=1;(*x)[0].tc=UINT32_MAX;(*y)[0].tc=UINT32_MAX;
    RequireShrink(!TVPLayerShrinkGeometry::CanUse32(operation),"RGB accumulation bound silently narrowed");
    (*x)[0].base=0;(*x)[0].ta=1;
    RequireShrink(TVPLayerShrinkGeometry::Validate(operation,1,1,1,1,false,validation)==TVPLayerShrinkResult::Arithmetic,
                  "nonzero alpha gate permitted out-of-bounds sample");
}
} // namespace

void ShrinkCopyTests(krkrsdl3::iTVPRenderBackend*) {
    shrinkLayerBindings={HasImage,Width,Height,Pitch,Resize};
    AliasProofTests();
    ArithmeticValidationTests();
    auto* cpu=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    // Binding inspection executes no getters, and detects a rebound ObjThis.
    {
        auto object=Object(gpu,4,4,Image(4,4,19));auto other=Object(gpu,4,4,Image(4,4,20));
        TestShrinkNativeLayer* native=nullptr;
        RequireShrink(TVPGetCanonicalShrinkLayer(object.get(),true,native),"canonical Layer bindings rejected");
        Property(object.get(),TJS_N("hasImage"),CustomHasImage);
        RequireShrink(!TVPGetCanonicalShrinkLayer(object.get(),false,native) && Layer(object.get())->customGets==0,
            "custom getter invoked during canonical inspection");
        Property(object.get(),TJS_N("hasImage"),HasImage,other.get());
        RequireShrink(!TVPGetCanonicalShrinkLayer(object.get(),false,native),"rebound property ObjThis accepted");
        Property(object.get(),TJS_N("hasImage"),HasImage);Method(object.get(),CustomResize);
        RequireShrink(!TVPGetCanonicalShrinkLayer(object.get(),true,native),"custom resize accepted");
    }
    struct AreaCase { double dx,dy,dw,dh;int sx,sy,sw,sh,tw,th; };
    const AreaCase cases[]={
        {0,0,4,3,0,0,13,11,8,7},{-1.25,-.35,7.4,5.6,-2,-1,15,12,8,7},
        {2.3,1.7,8.4,6.3,0,0,13,11,8,7},{.25,.75,1,7,0,0,13,11,8,9},
        {.25,.75,7,1,0,0,13,11,9,8},{.4,.3,1,1,0,0,13,11,3,3},
        {0,0,13.5,11.2,0,0,13,11,16,14},{.3,.1,3.4,2.3,2,2,9,7,8,7},
        {-100,-100,4,3,0,0,13,11,8,7}};
    unsigned comparisons=0;
    for(const auto& c:cases) for(bool diagnostics:{false,true}) {
        auto sourcePixels=Image(13,11,91),destPixels=Image(c.tw,c.th,31);
        auto source=Object(cpu,13,11,sourcePixels,true),reference=Object(cpu,c.tw,c.th,destPixels,true);
        auto resident=Object(gpu,13,11,sourcePixels),destination=Object(gpu,c.tw,c.th,destPixels);
        Layer(resident.get())->image->GetTexture()->GetTextureHandle();
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        krkrsdl3::layer_work::SetEnabled(diagnostics);
        RequireShrink(TJS_SUCCEEDED(Area(reference.get(),source.get(),c.dx,c.dy,c.dw,c.dh,c.sx,c.sy,c.sw,c.sh)),"CPU area rejected");
        const auto before=TVPGetMetalLayerRenderStats();
        RequireShrink(TJS_SUCCEEDED(Area(destination.get(),resident.get(),c.dx,c.dy,c.dw,c.dh,c.sx,c.sy,c.sw,c.sh)),"GPU area rejected");
        const auto after=TVPGetMetalLayerRenderStats();
        RequireShrink(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                      "resident supported Area crossed the CPU pixel boundary");
        krkrsdl3::layer_work::SetEnabled(false);
        Same(reference.get(),destination.get(),"GPU shrinkCopy differs from production CPU oracle");++comparisons;
        // Both unsigned-long widths exercise the exact production arithmetic.
        auto reference32=Object(cpu,c.tw,c.th,destPixels,true),reference64=Object(cpu,c.tw,c.th,destPixels,true);
        RequireShrink(TJS_SUCCEEDED(Area<uint32_t>(reference32.get(),source.get(),c.dx,c.dy,c.dw,c.dh,c.sx,c.sy,c.sw,c.sh)) &&
            TJS_SUCCEEDED(Area<uint64_t>(reference64.get(),source.get(),c.dx,c.dy,c.dw,c.dh,c.sx,c.sy,c.sw,c.sh)),"platform AvgT oracle rejected");
        Same(reference32.get(),reference64.get(),"small area platform width parity changed");
    }
    for(int x:{1,2,3,4,20}) for(int y:{1,2,3,4,20}) {
        auto sourcePixels=Image(13,11,61),old=Image(5,7,73);
        auto source=Object(cpu,13,11,sourcePixels,true),reference=Object(cpu,5,7,old,true);
        auto resident=Object(gpu,13,11,sourcePixels),destination=Object(gpu,5,7,old);
        Layer(resident.get())->image->GetTexture()->GetTextureHandle();
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        RequireShrink(TJS_SUCCEEDED(Fast(reference.get(),source.get(),x,y)) &&
            TJS_SUCCEEDED(Fast(destination.get(),resident.get(),x,y)),"Fast production entry rejected");
        RequireShrink(Layer(destination.get())->resizeCalls==1,"Fast resize called more than once");
        Same(reference.get(),destination.get(),"GPU Fast differs from two-pass floor CPU oracle");++comparisons;
    }
    for(const auto dimensions:{std::pair<int,int>{1,11},{13,1},{1,1}}) {
        const int w=dimensions.first,h=dimensions.second,tw=w==1?1:4,th=h==1?1:3;
        const auto pixels=Image(w,h,0xf7ffffff),old=Image(tw,th,7);
        auto source=Object(cpu,w,h,pixels,true),reference=Object(cpu,tw,th,old,true);
        auto resident=Object(gpu,w,h,pixels),destination=Object(gpu,tw,th,old);
        RequireShrink(TJS_SUCCEEDED(Area(reference.get(),source.get(),0,0,tw,th,0,0,w,h)) &&
            TJS_SUCCEEDED(Area(destination.get(),resident.get(),0,0,tw,th,0,0,w,h)),"single-axis source rejected");
        Same(reference.get(),destination.get(),"single-axis Area table allocation/math differs");++comparisons;
    }
    // This area genuinely needs 64-bit accumulation: 255*8192*8192 exceeds
    // uint32. The 32-bit oracle deliberately retains the original wrap.
    {
        const auto pixels=std::vector<uint32_t>(32*32,0xffffffffu);
        auto source32=Object(cpu,32,32,pixels,true),reference32=Object(cpu,1,1,{0},true);
        auto source64=Object(cpu,32,32,pixels,true),reference64=Object(cpu,1,1,{0},true);
        auto resident32=Object(gpu,32,32,pixels),destination32=Object(gpu,1,1,{0});
        auto resident64=Object(gpu,32,32,pixels),destination64=Object(gpu,1,1,{0});
        RequireShrink(TJS_SUCCEEDED(Area<uint32_t>(reference32.get(),source32.get(),0,0,1,1,0,0,32,32)) &&
            TJS_SUCCEEDED(Area<uint32_t>(destination32.get(),resident32.get(),0,0,1,1,0,0,32,32)),"32-bit wrapped Area rejected");
        RequireShrink(TJS_SUCCEEDED(Area<uint64_t>(reference64.get(),source64.get(),0,0,1,1,0,0,32,32)) &&
            TJS_SUCCEEDED(Area<uint64_t>(destination64.get(),resident64.get(),0,0,1,1,0,0,32,32)),"64-bit Area rejected");
        Same(reference32.get(),destination32.get(),"32-bit Area wrap was widened");
        Same(reference64.get(),destination64.get(),"64-bit Area was silently narrowed");
        RequireShrink(Read(reference32.get())[0]!=Read(reference64.get())[0],"wide Area fixture does not distinguish widths");
        comparisons+=2;
    }
    struct Typical {int sw,sh,dw,dh;};
    for(const auto c:{Typical{1280,720,128,72},Typical{1920,1080,496,279}}) {
        const auto pixels=Image(c.sw,c.sh,0x17059f3),old=Image(c.dw,c.dh,0x38);
        auto source=Object(cpu,c.sw,c.sh,pixels,true),reference=Object(cpu,c.dw,c.dh,old,true);
        auto resident=Object(gpu,c.sw,c.sh,pixels),destination=Object(gpu,c.dw,c.dh,old);
        Layer(resident.get())->image->GetTexture()->GetTextureHandle();
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        RequireShrink(TJS_SUCCEEDED(Area<uint64_t>(reference.get(),source.get(),0,0,c.dw,c.dh,0,0,c.sw,c.sh)),"typical CPU Area rejected");
        const auto before=TVPGetMetalLayerRenderStats();
        RequireShrink(TJS_SUCCEEDED(Area<uint64_t>(destination.get(),resident.get(),0,0,c.dw,c.dh,0,0,c.sw,c.sh)),"typical GPU Area rejected");
        const auto after=TVPGetMetalLayerRenderStats();
        RequireShrink(after.readbackBytes==before.readbackBytes && after.uploadedBytes==before.uploadedBytes,
                      "typical resident Area crossed CPU pixel boundary");
        Same(reference.get(),destination.get(),"typical Area differs from production CPU oracle");++comparisons;
    }
    // Horizontal floor must survive before the vertical average. Here a single
    // two-dimensional mean would yield one, while the plugin yields zero.
    {
        std::vector<uint32_t> pixels={0xff000000,0xff000001,0xff000001,0xff000002};
        auto source=Object(gpu,2,2,pixels),destination=Object(gpu,1,1,{0});
        RequireShrink(TJS_SUCCEEDED(Fast(destination.get(),source.get(),2,2)),"Fast floor counterexample rejected");
        RequireShrink(Read(destination.get())[0]==0xff000000,"Fast replaced two-axis floors with 2D average");++comparisons;
    }
    // Capture survives resize, while same-size aliases retain CPU ordering.
    for(int step:{1,2,3,20}) {
        const auto pixels=Image(13,11,17);
        auto reference=Object(cpu,13,11,pixels,true),destination=Object(gpu,13,11,pixels);
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        RequireShrink(TJS_SUCCEEDED(Fast(reference.get(),reference.get(),step,step)) &&
            TJS_SUCCEEDED(Fast(destination.get(),destination.get(),step,step)),"Fast self alias rejected");
        RequireShrink(Layer(destination.get())->resizeCalls==1,"Fast alias resize repeated");
        Same(reference.get(),destination.get(),"Fast alias read resized source");++comparisons;
    }
    // COW must sever real image sharing while the source capture stays alive.
    {
        const auto pixels=Image(13,11,87);
        auto destination=Object(gpu,13,11,pixels),peer=Object(gpu,13,11,pixels);
        auto reference=Object(cpu,13,11,pixels,true);
        auto* original=Layer(destination.get())->image->GetTexture();original->AddRef();
        Layer(peer.get())->image.reset(new TestShrinkBitmap(original,gpu));
        original->GetTextureHandle();
        RequireShrink(TJS_SUCCEEDED(Area(reference.get(),reference.get(),0,0,7,5,0,0,13,11)) &&
            TJS_SUCCEEDED(Area(destination.get(),destination.get(),0,0,7,5,0,0,13,11)),"shared Area rejected");
        Same(reference.get(),destination.get(),"Area COW refreshed source after cloning");
        RequireShrink(Read(peer.get())==pixels && Layer(destination.get())->image->GetTexture()!=original,
                      "Area modified shared bitmap peer");++comparisons;
    }
    // Rejection after one resize must read the captured old source and perform
    // CPU continuation without issuing a second script callback.
    {
        const auto pixels=Image(13,11,29),old=Image(5,7,71);
        auto referenceSource=Object(cpu,13,11,pixels,true),reference=Object(cpu,5,7,old,true);
        auto source=Object(gpu,13,11,pixels),destination=Object(gpu,5,7,old);
        auto* texture=Layer(source.get())->image->GetTexture();texture->GetTextureHandle();
        const void* leased=texture->LockCPURead();
        RequireShrink(leased && TJS_SUCCEEDED(Fast(reference.get(),referenceSource.get(),3,4)) &&
            TJS_SUCCEEDED(Fast(destination.get(),source.get(),3,4)),"Fast CPU-lease continuation failed");
        texture->UnlockCPU();
        RequireShrink(Layer(destination.get())->resizeCalls==1,"Fast fallback repeated resize");
        Same(reference.get(),destination.get(),"Fast fallback read new resized image");++comparisons;
    }
    // Non-finite geometry is rejected before a read or destination mutation.
    {
        auto source=Object(gpu,13,11,Image(13,11,8)),destination=Object(gpu,5,7,Image(5,7,9));
        const auto before=TVPGetMetalLayerRenderStats();
        RequireShrink(Area(destination.get(),source.get(),std::numeric_limits<double>::infinity(),0,3,3,0,0,13,11)==TJS_E_INVALIDPARAM,
                      "non-finite Area accepted");
        RequireShrink(TVPGetMetalLayerRenderStats().readbackBytes==before.readbackBytes,"invalid Area acquired pixels");
    }
    // Top-left shrink is dependency-safe; right/down shifts require CPU order.
    for(int offset:{0,1,3}) {
        const auto pixels=Image(13,11,43);
        auto reference=Object(cpu,13,11,pixels,true),destination=Object(gpu,13,11,pixels);
        RequireShrink(TJS_SUCCEEDED(Area(reference.get(),reference.get(),offset,offset,7,5,0,0,13,11)) &&
            TJS_SUCCEEDED(Area(destination.get(),destination.get(),offset,offset,7,5,0,0,13,11)),"Area self alias rejected");
        Same(reference.get(),destination.get(),"Area alias changed CPU sequential result");++comparisons;
    }
    std::cout<<"C1 production shrinkCopy CPU oracle: "<<comparisons<<" exact comparisons\n";
}

void ShrinkCopyFailureTests(const std::function<void(int,bool)>& inject) {
    shrinkLayerBindings={HasImage,Width,Height,Pitch,Resize};
    auto* cpu=TVPGetSoftwareRenderManager();auto* gpu=TVPGetRenderManager();
    for(int stage:{0,1,2,3}) for(bool fast:{false,true}) {
        const auto pixels=Image(13,11,0x1397),old=Image(5,7,0x2793);
        auto source=Object(cpu,13,11,pixels,true),reference=Object(cpu,5,7,old,true);
        auto resident=Object(gpu,13,11,pixels),destination=Object(gpu,5,7,old);
        Layer(resident.get())->image->GetTexture()->GetTextureHandle();
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        RequireShrink(TJS_SUCCEEDED(fast ? Fast(reference.get(),source.get(),3,4) :
            Area(reference.get(),source.get(),0,0,5,7,0,0,13,11)),"failure CPU oracle rejected");
        inject(stage,true);
        RequireShrink(TJS_SUCCEEDED(fast ? Fast(destination.get(),resident.get(),3,4) :
            Area(destination.get(),resident.get(),0,0,5,7,0,0,13,11)),"precommit failure did not continue on CPU");
        inject(-1,true);
        if(fast) RequireShrink(Layer(destination.get())->resizeCalls==1,"precommit Fast failure repeated resize");
        Same(reference.get(),destination.get(),"precommit failure changed exact CPU continuation");
    }
    for(bool fast:{false,true}) {
        const auto pixels=Image(13,11,0x7183),old=Image(5,7,0x1702);
        auto resident=Object(gpu,13,11,pixels),destination=Object(gpu,5,7,old);
        Layer(resident.get())->image->GetTexture()->GetTextureHandle();
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        const auto before=TVPGetMetalLayerRenderStats();
        inject(4,true);bool threw=false;
        try {
            if(fast) Fast(destination.get(),resident.get(),3,4);
            else Area(destination.get(),resident.get(),0,0,5,7,0,0,13,11);
        } catch(const std::bad_alloc&) {threw=true;}
        inject(-1,true);
        RequireShrink(threw,"postcommit exception was swallowed by CPU retry");
        RequireShrink(TVPGetMetalLayerRenderStats().readbackBytes==before.readbackBytes,
                      "postcommit failure ran a CPU readback retry");
        if(fast) RequireShrink(Layer(destination.get())->resizeCalls==1,"postcommit Fast failure repeated resize");
    }
    // A missing wide pipeline cannot silently narrow an unsafe 64-bit sum;
    // a separately proven small input continues through the 32-bit kernel.
    for(bool wide:{false,true}) {
        const int size=wide ? 32 : 4;
        const auto pixels=std::vector<uint32_t>(size_t(size)*size,0xffffffffu);
        auto source=Object(cpu,size,size,pixels,true),reference=Object(cpu,1,1,{0},true);
        auto resident=Object(gpu,size,size,pixels),destination=Object(gpu,1,1,{0});
        Layer(resident.get())->image->GetTexture()->GetTextureHandle();
        Layer(destination.get())->image->GetTexture()->GetTextureHandle();
        RequireShrink(TJS_SUCCEEDED(Area<uint64_t>(reference.get(),source.get(),0,0,1,1,0,0,size,size)),"missing-wide oracle rejected");
        const auto before=TVPGetMetalLayerRenderStats();
        inject(-1,false);
        RequireShrink(TJS_SUCCEEDED(Area<uint64_t>(destination.get(),resident.get(),0,0,1,1,0,0,size,size)),"missing-wide continuation failed");
        const auto after=TVPGetMetalLayerRenderStats();inject(-1,true);
        if(wide) RequireShrink(after.readbackBytes>before.readbackBytes,"unsafe wide Area was narrowed without fallback");
        else RequireShrink(after.readbackBytes==before.readbackBytes,"safe 64-to-32 Area unnecessarily fell back");
        Same(reference.get(),destination.get(),"missing-wide fallback changed Area pixels");
    }
    std::cout<<"C1 production shrinkCopy failures: 12 transactions verified\n";
}
