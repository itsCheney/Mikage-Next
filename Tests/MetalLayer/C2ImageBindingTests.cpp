#include "tjsCommHead.h"
#include "tjsNative.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "CPUConsumerTrace.h"
#include "ncbind/ncbind.hpp"
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <typeinfo>
#include <stdexcept>
#include <vector>
#include <memory>
#include <map>

namespace ImageDependency {
void Check(bool ok,const char* text) {if(!ok) throw std::runtime_error(text);}
bool ImageSupport=false,RejectImage=false,ThrowImage=false;
int ImageGPUCalls=0;
std::vector<std::string> ImageMessages;
void ImageLog(const char* message) {
    if(!std::strncmp(message,"metal.layerImage ",17)) ImageMessages.emplace_back(message);
}
struct TestImageClass {static constexpr int ClassID=0x213455;};
struct ImageCounts {int writes=0,unlocks=0,dirty=0;};
class ImageTexture:public iTVPTexture2D {
public:
    std::vector<uint32_t> pixels;
    std::shared_ptr<ImageCounts> counts=std::make_shared<ImageCounts>();
    tTVPRect lastDirty;
    ImageTexture(int w,int h):iTVPTexture2D(w,h),pixels(size_t(w)*h) {
        for(size_t i=0;i<pixels.size();++i) pixels[i]=uint32_t(i*0x123457u+0x80123456u);
        for(unsigned i=0;i<5;++i) pixels[size_t(5)*w+3+i]=uint32_t((i==4?255:i)*0x01000000u)|0x00553a19u;
    }
    TVPTextureFormat::e GetFormat() const override {return TVPTextureFormat::RGBA;}
    int GetPitch() const override {return Width*4;}
    void Update(const void*,TVPTextureFormat::e,int,const tTVPRect&) override {}
    uint32_t GetPoint(int x,int y) override {return pixels[size_t(y)*Width+x];}
    void SetPoint(int x,int y,uint32_t value) override {pixels[size_t(y)*Width+x]=value;}
    bool IsStatic() override {return false;} bool IsOpaque() override {return false;}
    bool GetTextureData(void*,int&) override {return false;}
    void* LockCPURead() override {return pixels.data();}
    void* LockCPUWrite() override {++counts->writes;return pixels.data();}
    void UnlockCPU() override {++counts->unlocks;}
    void UnlockCPUWrite(const tTVPRect& region) override {++counts->dirty;lastDirty=region;UnlockCPU();}
};
class TestImageLayer:public tTJSNativeInstance {
public:
    uint64_t GetLayerDiagnosticID() const { return 100; }
    ImageTexture* texture=new ImageTexture(32,32);
    tTVPRect clip{0,0,32,32};int updates=0,cow=0;bool modified=false,throwUpdate=false,throwPrepare=false;
    ~TestImageLayer() override {texture->Release();}
    int GetImageWidth() {return texture->GetWidth();} int GetImageHeight() {return texture->GetHeight();}
    int GetClipLeft() {return clip.left;} int GetClipTop() {return clip.top;}
    int GetClipWidth() {return clip.get_width();} int GetClipHeight() {return clip.get_height();}
    void Update(const tTVPRect&) {++updates;if(throwUpdate) throw std::runtime_error("update");}
    void SetImageModified(bool value) {modified=value;}
    iTVPTexture2D* GetMainImageTextureForSpanComposite() {
        if(throwPrepare) {throwPrepare=false;throw std::bad_alloc();}
        if(!texture->IsIndependent()) {
            auto* replacement=new ImageTexture(GetImageWidth(),GetImageHeight());replacement->pixels=texture->pixels;
            texture->Release();texture=replacement;++cow;
        }
        return texture;
    }
    iTVPTexture2D* GetMainImageTextureForCPUAccess(bool write) {
        if(write) {modified=true;GetMainImageTextureForSpanComposite();}return texture;
    }
};
bool ImageLUTSupport() {return ImageSupport;}
TVPLayerImageResult ImageLUT(iTVPTexture2D* target,const TVPLayerRect& r,const std::shared_ptr<const TVPLayerGammaLUT>& lut) {
    if(RejectImage) return TVPLayerImageResult::BackendFailure;
    auto* t=dynamic_cast<ImageTexture*>(target);Check(t!=nullptr,"Image target");++ImageGPUCalls;
    for(int y=r.top;y<r.bottom;++y) for(int x=r.left;x<r.right;++x) {
        auto& p=t->pixels[size_t(y)*t->GetWidth()+x];
        p=(p&0xff000000u)|lut->bytes[p&255]|(uint32_t(lut->bytes[256+((p>>8)&255)])<<8)|
            (uint32_t(lut->bytes[512+((p>>16)&255)])<<16);
    }
    if(ThrowImage) throw std::runtime_error("post write");
    return TVPLayerImageResult::Applied;
}
}
using namespace ImageDependency;
#define TVPHasMetalLayerImageLUTSupport ImageLUTSupport
#define TVPTryMetalLayerImageLUT ImageLUT
#define layerExImage C2ImagePlugin
#define layerExBase_GL C2ImageBase
#define tTVPScopedLayerPixels C2ImagePixels
#include "ProductionLayerExImage.inc"
#undef TVPHasMetalLayerImageLUTSupport
#undef TVPTryMetalLayerImageLUT
#undef layerExImage
#undef layerExBase_GL
#undef tTVPScopedLayerPixels

namespace {
struct ImageFixture {
    iTJSDispatch2* object=new tTJSCustomObject;
    TestImageLayer* layer=new TestImageLayer;
    ImageFixture() {iTJSNativeInstance* native=layer;object->NativeInstanceSupport(TJS_NIS_REGISTER,TestImageClass::ClassID,&native);}
    ~ImageFixture() {object->Invalidate(0,nullptr,nullptr,object);object->Release();}
};
struct ImageRegistration:ncbRegistNativeClassBase {
    using NativeClassT=C2ImagePlugin;std::map<std::string,iTJSDispatch2*> methods;
    ImageRegistration():ncbRegistNativeClassBase(TJS_N("C2Image")) {}
    void RegistItem(NameT name,ItemT item) {methods[ttstr(name).AsStdString()]=item->GetDispatch();item->Release();}
    ~ImageRegistration() {for(auto& item:methods)item.second->Release();}
};
std::vector<uint32_t> LightOracle(std::vector<uint32_t> pixels,const tTVPRect& r,int b,int contrast,int width) {
    // Original CPU formula, independent of the production LUT helper/backend.
    uint8_t table[256];const float c=(100+contrast)/100.0f;b+=128;
    for(int i=0;i<256;++i) table[i]=uint8_t(std::max(0,std::min(255,int((i-128)*c+b))));
    for(int y=r.top;y<r.bottom;++y) for(int x=r.left;x<r.right;++x) {
        auto& p=pixels[size_t(y)*width+x];
        p=(p&0xff000000u)|table[p&255]|(uint32_t(table[(p>>8)&255])<<8)|(uint32_t(table[(p>>16)&255])<<16);
    }
    return pixels;
}
}
int RunC2ImageBindingTests() {
    namespace work=krkrsdl3::layer_work;namespace trace=krkrsdl3::cpu_consumer_trace;
    auto* previousLog=trace::logMessage;
    try {
        ncbClassInfo<C2ImagePlugin>::Set(TJS_N("C2Image"),0x42135c,nullptr);
        ImageRegistration registration;RegisterImageMethods(registration);
        auto call=[&](ImageFixture& f,const char* name,std::vector<tTJSVariant> values) {
            std::vector<tTJSVariant*> args;for(auto& v:values) args.push_back(&v);
            return registration.methods.at(name)->FuncCall(0,nullptr,nullptr,nullptr,args.size(),args.data(),f.object);
        };
        trace::logMessage=ImageLog;
        for(bool diagnostics:{false,true}) for(bool support:{false,true}) for(int b:{-255,-31,0,17,255}) for(int c:{-100,-17,0,33,100}) {
            work::SetEnabled(diagnostics);ImageSupport=support;
            ImageFixture f;f.layer->clip={2,3,27,28};
            const auto expected=LightOracle(f.layer->texture->pixels,f.layer->clip,b,c,32);
            Check(TJS_SUCCEEDED(call(f,"light",{tTJSVariant(b),tTJSVariant(c),tTJSVariant(99)})),"Image light minimum arity/trailing arg");
            Check(f.layer->texture->pixels==expected,"Image light exact RGB/alpha/ROI mismatch");
            Check(f.layer->texture->counts->writes==(support?0:1),"Image constructor acquired pixels / GPU acquired CPU");
            Check(f.layer->texture->counts->unlocks==(support?0:1) && f.layer->updates==1 && f.layer->modified,"Image lease/update semantics");
        }
        ImageSupport=true;work::SetEnabled(true);
        {
            ImageFixture f;const auto original=f.layer->texture->pixels;
            Check(TJS_SUCCEEDED(call(f,"light",{tTJSVariant(TJS_N("13")),tTJSVariant(TJS_N("0"))})),"Image numeric string conversion changed");
            Check(f.layer->texture->counts->writes==1 && f.layer->texture->pixels==LightOracle(original,f.layer->clip,13,0,32),"Image complex conversion skipped original CPU lease/order");
        }
        {
            ImageFixture f;const auto original=f.layer->texture->pixels;
            Check(call(f,"light",{tTJSVariant(1)})==TJS_E_BADPARAMCOUNT,"Image bad arity changed");
            Check(!ncbInstanceAdaptor<C2ImagePlugin>::GetNativeInstance(f.object,false) && f.layer->texture->counts->writes==0,"Image arity allocated/acquired");
            RejectImage=true;Check(TJS_SUCCEEDED(call(f,"light",{tTJSVariant(3),tTJSVariant(5)})),"Image rejection failed CPU replay");RejectImage=false;
            Check(f.layer->texture->counts->writes==1 && f.layer->texture->pixels==LightOracle(original,f.layer->clip,3,5,32),"Image rejection replayed more than once");
        }
        {
            ImageFixture f;auto* shared=f.layer->texture;shared->AddRef();const auto old=shared->pixels;
            Check(TJS_SUCCEEDED(call(f,"light",{tTJSVariant(10),tTJSVariant(0)})) && f.layer->cow==1,"Image GPU COW not applied");
            Check(shared->pixels==old && f.layer->texture->counts->writes==0,"Image GPU touched shared source");shared->Release();
        }
        {
            ImageFixture f;const auto original=f.layer->texture->pixels;ThrowImage=true;
            bool threw=false;try {call(f,"light",{tTJSVariant(17),tTJSVariant(11)});} catch(...) {threw=true;}ThrowImage=false;
            Check(threw && f.layer->texture->counts->writes==0 && f.layer->updates==0,"Image backend exception replayed CPU");
            Check(f.layer->texture->pixels==LightOracle(original,f.layer->clip,17,11,32),"Image backend post write repeated operation");
        }
        {
            ImageFixture f;f.layer->throwUpdate=true;bool threw=false;const auto before=ImageGPUCalls;
            try {call(f,"light",{tTJSVariant(1),tTJSVariant(2)});} catch(...) {threw=true;}
            Check(threw && ImageGPUCalls==before+1 && f.layer->texture->counts->writes==0,"Image update failure replayed GPU/CPU");
        }
        {
            ImageFixture f;f.layer->throwPrepare=true;
            Check(TJS_SUCCEEDED(call(f,"light",{tTJSVariant(1),tTJSVariant(2)})) && f.layer->texture->counts->writes==1,"Image allocation failure CPU continuation");
        }
        for(const auto* method:{"modulate","colorize","noise","generateWhiteNoise"}) {
            std::vector<tTJSVariant> args;
            if(!std::strcmp(method,"modulate")) args={tTJSVariant(30),tTJSVariant(17),tTJSVariant(-9)};
            if(!std::strcmp(method,"colorize")) args={tTJSVariant(30),tTJSVariant(150),tTJSVariant(0.4)};
            if(!std::strcmp(method,"noise")) args={tTJSVariant(23)};
            std::vector<uint32_t> reference;
            for(bool enabled:{false,true}) {
                work::SetEnabled(enabled);ImageFixture f;std::srand(1234);const int gpu=ImageGPUCalls;
                Check(TJS_SUCCEEDED(call(f,method,args)),"Image legacy method failed");
                Check(f.layer->texture->counts->writes==1 && f.layer->texture->counts->unlocks==1 && ImageGPUCalls==gpu,"Image legacy lease/random route changed");
                if(!enabled) reference=f.layer->texture->pixels;
                else Check(reference==f.layer->texture->pixels,"Image diagnostics changed CPU/random pixels");
            }
        }
        work::Take();Check(!ImageMessages.empty(),"Image diagnostics not emitted");
        for(const auto& m:ImageMessages) Check(m.size()<=900,"Image diagnostic line oversized");
        {
            namespace image=krkrsdl3::layer_image;
            image::Window w;image::Context c;c.method="light";c.metrics={1,1,4,100,80,768};
            for(int i=0;i<70;++i) {std::snprintf(c.parameters,sizeof(c.parameters),"[%d,0]",i);w.Record(c);}
            Check(w.size==64 && w.calls==70 && w.capacityRecords==6 && w.overflow.readWaitNS==480 &&
                w.totals.readBytes==280,"Image parameter overflow lost totals");
            std::string oversized(32,'x');c.method=oversized.c_str();w.Record(c);
            Check(w.oversizeRecords==1 && w.totals.calls==71,"Image oversized name truncated/merged");
            image::Window saturated;c.method="light";c.metrics.readBytes=UINT64_MAX;saturated.Record(c);
            c.metrics.readBytes=1;saturated.Record(c);
            Check(saturated.saturated && saturated.totals.readBytes==UINT64_MAX,"Image metrics overflow wrapped");
            work::SetEnabled(true);image::Context current;current.method="light";current.metrics.calls=1;
            trace::BeginImage(current);image::context=&current;
            work::Record(false,7,2,2,16,100,90,false,"layerExImage.write",current.epoch);
            trace::RecordImage(current);
            Check(current.metrics.readCalls==1 && current.metrics.readWaitNS==90,"Image missed measured C0 read wait");
            work::Take();trace::RecordImage(current);
            Check(work::profile.cpuConsumerBudget.imageWindow.lateCalls==1,"Image late call entered wrong window");
            work::Record(false,8,2,2,16,100,90,false,"layerExImage.write",current.epoch);
            Check(work::profile.cpuConsumerBudget.imageWindow.lateReads==1,"Image late read entered wrong window");
            work::SetEnabled(false);work::SetEnabled(true);trace::RecordImage(current);
            Check(work::profile.cpuConsumerBudget.imageWindow.totals.calls==0,"Image stale generation polluted new session");
            image::context=nullptr;
        }
        work::SetEnabled(false);trace::logMessage=previousLog;
        Check(krkrsdl3::layer_image::context==nullptr,"Image invocation context leaked");
        std::cout<<"C2 Image production NCBind/CPU oracle tests passed (GPU dependency doubled)\n";return 0;
    } catch(const std::exception& e) {
        work::SetEnabled(false);trace::logMessage=previousLog;
        std::cerr<<"C2 Image: "<<e.what()<<'\n';return 1;
    }
}
