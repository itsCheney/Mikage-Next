#include "tjsCommHead.h"
#include "tjsNative.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "CPUConsumerTrace.h"
#include "ncbind/ncbind.hpp"
#include "DrawBackend.h"
#include "SpanCapture.h"
#include "ProductionSpanMath.inc"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeinfo>
#include <vector>
#define LayerExDraw C2BLayerExDraw
#define layerExBase_GL C2BLayerExBase
#define tTVPScopedLayerPixels C2BScopedLayerPixels
#define Appearance C2BAppearance
#define Path C2BPath
#define GdipImage C2BGdipImage
#define GdipWrapper C2BGdipWrapper
#define GdipTypeConvertor C2BGdipTypeConvertor
#define MatrixConvertor C2BMatrixConvertor
#define IsArray C2BIsArray
#define GdipMatrix C2BGdipMatrix
#define FontInfo C2BFontInfo
#define RectF C2BRectF
#define PointF C2BPointF
#define TestDrawClass C2BTestDrawClass
#define TestDrawTexture C2BTestDrawTexture
#define TestDrawNativeLayer C2BTestDrawNativeLayer
void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct TestDrawClass {static constexpr int ClassID=0x421356;};
struct Counts {int reads=0,writes=0,unlocks=0,dirty=0;};
class TestDrawTexture:public iTVPTexture2D {
public:
    std::vector<uint32_t> pixels;
    std::shared_ptr<Counts> counts=std::make_shared<Counts>();
    uint64_t identity;
    static uint64_t nextID;
    TestDrawTexture(int w,int h):iTVPTexture2D(w,h),pixels(size_t(w)*h,0x90604830u),identity(++nextID) {}
    TVPTextureFormat::e GetFormat() const override {return TVPTextureFormat::RGBA;}
    int GetPitch() const override {return Width*4;}
    void Update(const void*,TVPTextureFormat::e,int,const tTVPRect&) override {}
    uint32_t GetPoint(int x,int y) override {return pixels[size_t(y)*Width+x];}
    void SetPoint(int x,int y,uint32_t c) override {pixels[size_t(y)*Width+x]=c;}
    bool IsStatic() override {return false;}
    bool IsOpaque() override {return false;}
    bool GetTextureData(void*,int&) override {return false;}
    bool GetContentKey(uint64_t& id,uint64_t& version) const override {id=identity;version=counts->dirty;return true;}
    void* LockCPURead() override {++counts->reads;return pixels.data();}
    void* LockCPUWrite() override {++counts->writes;return pixels.data();}
    void UnlockCPU() override {++counts->unlocks;}
    void UnlockCPUWrite(const tTVPRect&) override {++counts->dirty;UnlockCPU();}
};
uint64_t TestDrawTexture::nextID=0;
class TestDrawNativeLayer:public tTJSNativeInstance {
public:
    TestDrawTexture* texture;
    int updates=0,cow=0;bool modified=false,throwPrepare=false;
    tTVPRect clip;
    explicit TestDrawNativeLayer(int w,int h):texture(new TestDrawTexture(w,h)),clip(0,0,w,h) {}
    ~TestDrawNativeLayer() override {texture->Release();}
    int GetImageWidth() const {return texture->GetWidth();}
    int GetImageHeight() const {return texture->GetHeight();}
    int GetClipLeft() const {return clip.left;} int GetClipTop() const {return clip.top;}
    int GetClipWidth() const {return clip.get_width();} int GetClipHeight() const {return clip.get_height();}
    void Update() {++updates;} void Update(const tTVPRect&) {++updates;}
    void SetImageModified(bool v) {modified=v;}
    iTVPTexture2D* GetMainImageTextureForSpanComposite() {
        if(throwPrepare) {throwPrepare=false;throw std::runtime_error("prepare");}
        if(!texture->IsIndependent()) {
            auto* next=new TestDrawTexture(GetImageWidth(),GetImageHeight());next->pixels=texture->pixels;
            texture->Release();texture=next;++cow;
        }
        return texture;
    }
    iTVPTexture2D* GetMainImageTextureForCPUAccess(bool write) {
        if(write) modified=true;return GetMainImageTextureForSpanComposite();
    }
};
bool Support=false,RejectSubmit=false,ThrowAfterCommit=false;
int Attempts=0;
bool TestSpanSupport() {return Support;}
TVPLayerSpanCompositeResult TestSpanSubmit(const TVPLayerSpanCompositePacket& p,iTVPTexture2D* target) {
    ++Attempts;auto* t=static_cast<TestDrawTexture*>(target);
    if(RejectSubmit) return TVPLayerSpanCompositeResult::Resource;
    auto output=t->pixels;
    for(int y=p.destination.top;y<p.destination.bottom;++y) for(int x=p.destination.left;x<p.destination.right;++x)
        output[size_t(y)*t->GetWidth()+x]=span_shader::spanComposePixel(t->pixels[size_t(y)*t->GetWidth()+x],x,y,y-p.destination.top,
            p.spans.data(),p.sourcePixels.data(),p.rowOffsets.data(),p.rowEntries.data());
    t->pixels.swap(output);++t->counts->dirty;
    if(ThrowAfterCommit) throw std::runtime_error("postcommit");
    return TVPLayerSpanCompositeResult::Applied;
}
enum SmoothingMode {SmoothingModeAntiAlias=4};
enum TextRenderingHint {TextRenderingHintAntiAlias=4};
enum Status {Ok,GenericError}; enum ImageType {ImageTypeBitmap,ImageTypeUnknown,ImageTypeMetafile};
enum RotateFlipType {RotateNoneFlipNone};static constexpr int PixelFormat32bppARGB=0;
struct RectF {tjs_real x=0,y=0,w=0,h=0;RectF()=default;RectF(tjs_real X,tjs_real Y,tjs_real W,tjs_real H):x(X),y(Y),w(W),h(H) {}};
struct PointF {};
struct GdipMatrix {plutovg_matrix_t _core;GdipMatrix(){plutovg_matrix_init_identity(&_core);}
    explicit GdipMatrix(plutovg_matrix_t v):_core(v){} GdipMatrix* Clone() const {return new GdipMatrix(_core);}};
struct FontInfo {plutovg_font_face_t* getFontFace() const {return nullptr;} float getEmSize() const {return 10;}};
NCB_TYPECONV_SRCMAP_SET(RectF,ncbNativeObjectBoxing::Boxing,true);
#define TVPHasMetalLayerSpanCompositionSupport TestSpanSupport
#define TVPTryMetalLayerSpanComposite TestSpanSubmit
#include "ProductionLayerExSpanBinding.inc"
#undef TVPTryMetalLayerSpanComposite
#undef TVPHasMetalLayerSpanCompositionSupport
struct LayerFixture {
    iTJSDispatch2* object=new tTJSCustomObject;
    TestDrawNativeLayer* native;
    LayerFixture():native(new TestDrawNativeLayer(67,39)) {
        iTJSNativeInstance* instance=native;object->NativeInstanceSupport(TJS_NIS_REGISTER,TestDrawClass::ClassID,&instance);
    }
    ~LayerFixture() {object->Invalidate(0,nullptr,nullptr,object);object->Release();}
    LayerExDraw* Draw() {return ncbInvocationPolicy<LayerExDraw>::Scope::Instance(object);}
};
struct Registration:ncbRegistNativeClassBase {
    using NativeClassT=LayerExDraw;
    std::map<std::string,iTJSDispatch2*> methods;
    Registration():ncbRegistNativeClassBase(TJS_N("LayerExDraw")){}
    void RegistItem(NameT name,ItemT item) {methods[ttstr(name).AsStdString()]=item->GetDispatch();item->Release();}
    ~Registration(){for(auto& v:methods)v.second->Release();}
};
int ResultConstructors=0;
bool ThrowResultConstructor=false;
class C2BResultClass:public tTJSNativeClassForPlugin {
public:
    C2BResultClass():tTJSNativeClassForPlugin(ttstr(TJS_N("C2BRect")),nullptr) {}
    tjs_error CreateNew(tjs_uint32,const tjs_char*,tjs_uint32*,iTJSDispatch2** result,tjs_int,tTJSVariant**,iTJSDispatch2*) override {
        ++ResultConstructors;
        if(ThrowResultConstructor) throw std::runtime_error("result constructor");
        auto* object=new tTJSCustomObject;
        iTJSNativeInstance* adaptor=ncbInstanceAdaptor<RectF>::CreateEmptyAdaptor();
        object->NativeInstanceSupport(TJS_NIS_REGISTER,ncbClassInfo<RectF>::GetID(),&adaptor);
        *result=object;return TJS_S_OK;
    }
};
template<class T> tTJSVariant Box(T* value) {
    auto* obj=new tTJSCustomObject;
    iTJSNativeInstance* adp=ncbInstanceAdaptor<GdipWrapper<T>>::CreateEmptyAdaptor();
    obj->NativeInstanceSupport(TJS_NIS_REGISTER,ncbClassInfo<GdipWrapper<T>>::GetID(),&adp);
    ncbInstanceAdaptor<GdipWrapper<T>>::SetNativeInstance(obj,new GdipWrapper<T>(value));
    tTJSVariant result(obj,obj);obj->Release();return result;
}
int RunC2SpanBindingTests() {
    try {
        ncbClassInfo<LayerExDraw>::Set(TJS_N("C2BLayer"),0x421357,nullptr);
        ncbClassInfo<GdipWrapper<Appearance>>::Set(TJS_N("C2BApp"),0x421358,nullptr);
        ncbClassInfo<GdipWrapper<Path>>::Set(TJS_N("C2BPath"),0x421359,nullptr);
        ncbClassInfo<GdipWrapper<GdipImage>>::Set(TJS_N("C2BImage"),0x42135a,nullptr);
        std::unique_ptr<C2BResultClass,void(*)(C2BResultClass*)> resultClass(new C2BResultClass,
            [](C2BResultClass* p){ncbClassInfo<RectF>::Clear();p->Release();});
        ncbClassInfo<RectF>::Set(TJS_N("C2BRect"),0x42135b,resultClass.get());
        Registration registration;
        {ncbRegistClass<Registration> r(registration,true);r.Method(TJS_N("drawLine"),&LayerExDraw::drawLine);
            r.Method(TJS_N("drawPath"),&LayerExDraw::drawPath);r.Method(TJS_N("drawImageStretch"),&LayerExDraw::drawImageStretch);
            r.Method(TJS_N("clear"),&LayerExDraw::clear);}
        auto call=[&](LayerFixture& f,const char* name,std::vector<tTJSVariant> values,tTJSVariant* result=nullptr) {
            std::vector<tTJSVariant*> args;for(auto& v:values)args.push_back(&v);
            return registration.methods.at(name)->FuncCall(0,nullptr,nullptr,result,args.size(),args.data(),f.object);
        };
        auto* app=new Appearance;
        app->drawInfos.emplace_back();app->drawInfos.back().info=new SoftPen(0x9162a430u,4.3);app->drawInfos.back().type=0;
        app->drawInfos.emplace_back();app->drawInfos.back().info=new SoftBrush(0x635080a0u);app->drawInfos.back().type=1;
        auto appearance=Box(app);
        auto* path=new Path;plutovg_path_add_ellipse(const_cast<plutovg_path_t*>(path->spanCapturePath()),30.2f,17.4f,18.3f,10.1f);
        auto pathArg=Box(path);
        auto* srcSurface=plutovg_surface_create(9,7);
        std::fill_n(reinterpret_cast<uint32_t*>(plutovg_surface_get_data(srcSurface)),63,0x80402010u);
        auto imageArg=Box(new GdipImage(srcSurface));plutovg_surface_destroy(srcSurface);
        std::vector<std::pair<const char*,std::vector<tTJSVariant>>> operations={
            {"drawLine",{appearance,5.3,8.2,59.4,28.7}},
            {"drawPath",{appearance,pathArg}},
            {"drawImageStretch",{7.3,4.2,48.7,27.4,imageArg,-2.1,0.0,13.3,7.8}}};
        for(const auto& op:operations) {
            LayerFixture cpu,gpu;tTJSVariant cpuResult,gpuResult;
            Support=false;call(cpu,op.first,op.second,&cpuResult);
            Support=true;const int attempts=Attempts;call(gpu,op.first,op.second,&gpuResult);
            Check(Attempts==attempts+1 && gpu.native->texture->counts->writes==0 && gpu.native->texture->counts->reads==0,"eligible binding acquired target CPU pixels");
            if(cpu.native->texture->pixels!=gpu.native->texture->pixels) {
                auto mismatch=std::mismatch(cpu.native->texture->pixels.begin(),cpu.native->texture->pixels.end(),gpu.native->texture->pixels.begin());
                std::cerr<<"method="<<op.first<<" pixel="<<std::distance(cpu.native->texture->pixels.begin(),mismatch.first)
                    <<" cpu="<<std::hex<<*mismatch.first<<" gpu="<<*mismatch.second<<std::dec
                    <<" cpuWrites="<<cpu.native->texture->counts->writes<<" cpuUpdates="<<cpu.native->updates<<'\n';
            }
            Check(cpu.native->texture->pixels==gpu.native->texture->pixels,"production binding captured pixels differ");
            Check(cpu.native->updates==gpu.native->updates && gpu.native->modified,"GPU commit metadata/update mismatch");
            auto* cpuRect=ncbInstanceAdaptor<RectF>::GetNativeInstance(cpuResult.AsObjectNoAddRef());
            auto* gpuRect=ncbInstanceAdaptor<RectF>::GetNativeInstance(gpuResult.AsObjectNoAddRef());
            Check(cpuRect && gpuRect && cpuRect->x==gpuRect->x && cpuRect->y==gpuRect->y &&
                cpuRect->w==gpuRect->w && cpuRect->h==gpuRect->h,"GPU typed drawing result differs from CPU");
            LayerFixture rejected;RejectSubmit=true;call(rejected,op.first,op.second);RejectSubmit=false;
            Check(rejected.native->texture->pixels==cpu.native->texture->pixels && rejected.native->texture->counts->writes==1 && rejected.native->updates==1,"precommit rejection did not replay CPU exactly once");
        }
        {
            LayerFixture cpu,gpu;Support=false;call(cpu,"drawPath",operations[1].second);
            Support=true;call(gpu,"drawPath",operations[1].second);
            cpu.native->clip=gpu.native->clip=tTVPRect(8,5,49,29);
            Support=false;call(cpu,"drawPath",operations[1].second);call(gpu,"drawPath",operations[1].second);
            cpu.native->clip=gpu.native->clip=tTVPRect(0,0,67,39);
            call(cpu,"drawPath",operations[1].second);Support=true;call(gpu,"drawPath",operations[1].second);
            Check(cpu.native->texture->pixels==gpu.native->texture->pixels,"CPU/GPU crossing reset historical clip intersection");
        }
        {
            LayerFixture target;Support=false;
            std::unique_ptr<GdipImage> escaped(target.Draw()->getImageForBridge());
            const int attempts=Attempts;Support=true;call(target,"drawPath",operations[1].second);
            Check(Attempts==attempts && target.native->texture->counts->writes==2,"escaped target alias reached GPU");
            std::unique_ptr<GdipImage> clone(escaped->Clone());
            Check(std::memcmp(plutovg_surface_get_data(clone->_surface),target.native->texture->pixels.data(),67*39*4)==0,"escaped surface alias/read changed");
        }
        {
            LayerFixture target;Support=true;target.native->throwPrepare=true;
            const int writes=target.native->texture->counts->writes;call(target,"drawPath",operations[1].second);
            Check(target.native->texture->counts->writes==writes+1 && target.native->updates==1,"preparation exception replay mismatch");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            auto empty=Box(new Appearance);
            call(target,"drawLine",{empty,2.0,4.0,40.0,16.0});
            Check(Attempts==attempts && target.native->texture->counts->writes==0 && target.native->updates==1 && target.native->modified,
                "empty span noop acquired CPU or missed legacy Update/ImageModified");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            std::vector<uint32_t> borrowedPixels(63,0x80402010u);
            auto* borrowed=plutovg_surface_create_for_data(reinterpret_cast<unsigned char*>(borrowedPixels.data()),9,7,36);
            auto borrowedArg=Box(new GdipImage(borrowed));plutovg_surface_destroy(borrowed);
            call(target,"drawImageStretch",{7.3,4.2,48.7,27.4,borrowedArg,0.0,0.0,9.0,7.0});
            Check(Attempts==attempts && target.native->texture->counts->writes==1,"borrowed image source skipped CPU acquisition");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            target.Draw()->setRecord(true);call(target,"drawPath",operations[1].second);
            Check(Attempts==attempts && target.native->texture->counts->writes==1 && target.native->updates==1,"record draw entered capture or duplicated Update");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            LayerExDraw::InvocationPixels outer(*target.Draw());call(target,"drawPath",operations[1].second);
            Check(Attempts==attempts && target.native->texture->counts->writes==1,"active invocation lease entered capture/reacquired");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            auto values=operations[0].second;values[1]=TJS_N("5.3");call(target,"drawLine",values);
            Check(Attempts==attempts && target.native->texture->counts->writes==1,"nonprimitive numeric converter skipped legacy eager acquisition");
            Check(call(target,"drawLine",{})==TJS_E_BADPARAMCOUNT && target.native->texture->counts->writes==1,
                "malformed argument count acquired pixels or changed error");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            auto* frozen=target.native->texture;frozen->AddRef();auto old=frozen->pixels;
            call(target,"drawPath",operations[1].second);
            Check(Attempts==attempts+1 && target.native->cow==1 && frozen->pixels==old && target.native->texture->counts->writes==0,
                "GPU capture COW changed shared source or acquired pixels");frozen->Release();
        }
        {
            LayerFixture target;Support=true;ThrowAfterCommit=true;bool threw=false;
            try {call(target,"drawPath",operations[1].second);}catch(...){threw=true;}ThrowAfterCommit=false;
            Check(threw && target.native->texture->counts->writes==0,"postcommit failure replayed CPU");
        }
        {
            LayerFixture target;Support=true;RejectSubmit=true;const int before=ResultConstructors;
            tTJSVariant result;call(target,"drawPath",operations[1].second,&result);RejectSubmit=false;
            Check(ResultConstructors==before+1 && result.Type()==tvtObject && target.native->texture->counts->writes==1,
                "precommit rejection duplicated result constructor side effects");
        }
        {
            LayerFixture target;Support=true;const int before=ResultConstructors;
            tTJSVariant result;call(target,"drawPath",operations[1].second,&result);
            Check(ResultConstructors==before+1 && result.Type()==tvtObject && target.native->texture->counts->writes==0,
                "accepted capture did not box result once after commit");
            auto* rect=ncbInstanceAdaptor<RectF>::GetNativeInstance(result.AsObjectNoAddRef());
            Check(rect && rect->x==0 && rect->y==0 && rect->w==0 && rect->h==0,"typed result changed");
        }
        {
            LayerFixture target;Support=true;ThrowResultConstructor=true;bool threw=false;
            tTJSVariant result;try {call(target,"drawPath",operations[1].second,&result);}catch(...){threw=true;}
            ThrowResultConstructor=false;
            Check(threw && target.native->texture->counts->writes==0 && target.native->updates==1,
                "postcommit result constructor exception replayed CPU/Update");
        }
        std::cout<<"C2B real plutovg production binding transaction checks passed\n";Support=false;return 0;
    }catch(const std::exception& error){std::cerr<<"C2B binding: "<<error.what()<<'\n';return 1;}
}
