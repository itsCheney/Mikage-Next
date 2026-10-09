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
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeinfo>
#include <type_traits>
#include <vector>
// Platform logging is the only extra service used by real NCBind registration.
void TVPAddLog(const ttstr&) {}
#define LayerExDraw C2BLayerExDraw
#define layerExBase_GL C2BLayerExBase
#define tTVPScopedLayerPixels C2BScopedLayerPixels
#define Appearance C2BAppearance
#define Path C2BPath
#define GdipImage C2BGdipImage
#define GdipWrapper C2BGdipWrapper
#define GdipTypeConvertor C2BGdipTypeConvertor
#define ImageConvertor C2BImageConvertor
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
int ImageLoads=0;
plutovg_surface_t* ImageLoadSurface=nullptr;
// File loading is a dependency double; the production ImageConvertor below
// must invoke it only on the legacy route, exactly once per call.
static plutovg_surface_t* loadImage(const tjs_char*) {++ImageLoads;return ImageLoadSurface;}
#define TVPHasMetalLayerSpanCompositionSupport TestSpanSupport
#define TVPTryMetalLayerSpanComposite TestSpanSubmit
#include "ProductionLayerExSpanBinding.inc"
#undef TVPTryMetalLayerSpanComposite
#undef TVPHasMetalLayerSpanCompositionSupport
static_assert(std::is_same<ncbTypeConvertor::SelectConvertorType<tTJSVariant,const Appearance*>::Type,
    ncbNativeObjectBoxing::Unboxing>::value,"Appearance must use production direct-native unboxing");
static_assert(std::is_same<ncbTypeConvertor::SelectConvertorType<tTJSVariant,const Path*>::Type,
    ncbNativeObjectBoxing::Unboxing>::value,"Path must use production direct-native unboxing");
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
// Image alone uses the production Gdip wrapper native type.
tTJSVariant BoxImage(GdipImage* value) {
    auto* obj=new tTJSCustomObject;
    iTJSNativeInstance* adp=ncbInstanceAdaptor<GdipWrapper<GdipImage>>::CreateEmptyAdaptor();
    obj->NativeInstanceSupport(TJS_NIS_REGISTER,ncbClassInfo<GdipWrapper<GdipImage>>::GetID(),&adp);
    ncbInstanceAdaptor<GdipWrapper<GdipImage>>::SetNativeInstance(obj,new GdipWrapper<GdipImage>(value));
    tTJSVariant result(obj,obj);obj->Release();return result;
}
template<class T> tTJSVariant ConstructDirect() {
    iTJSDispatch2* object=nullptr;
    Check(TJS_SUCCEEDED(ncbClassInfo<T>::GetClassObject()->CreateNew(0,nullptr,nullptr,&object,0,nullptr,nullptr)) && object,
        "registered direct-native constructor failed");
    tTJSVariant result(object,object);object->Release();return result;
}
template<class T> T* UnboxDirect(const tTJSVariant& value) {
    typename ncbTypeConvertor::SelectConvertorType<tTJSVariant,T*>::Type converter;
    T* result=nullptr;converter(result,value);return result;
}
template<class T> tTJSVariant BoxDirect(T* value) {
    typename ncbTypeConvertor::SelectConvertorType<T*,tTJSVariant>::Type converter;
    tTJSVariant result;converter(result,value,ncbTypedefs::Tag<T*>());return result;
}
template<class T> struct DirectRegistration {
    const tjs_char* name;
    DirectRegistration(const tjs_char* value):name(value) {Check(ncbSubClassItem<T>::Setup(name,true),"direct subclass registration failed");}
    ~DirectRegistration() {ncbSubClassItem<T>::Setup(name,false);}
};
class GuardedNativeObject:public tTJSCustomObject {
public:
    int nativeGets=0,propertyGets=0;
    tjs_error NativeInstanceSupport(tjs_uint32 flag,tjs_int32 id,iTJSNativeInstance** instance) override {
        if(flag==TJS_NIS_GETINSTANCE) ++nativeGets;
        return tTJSCustomObject::NativeInstanceSupport(flag,id,instance);
    }
    tjs_error PropGet(tjs_uint32 flag,const tjs_char* name,tjs_uint32* hint,tTJSVariant* result,iTJSDispatch2* object) override {
        ++propertyGets;return tTJSCustomObject::PropGet(flag,name,hint,result,object);
    }
};
class UnusedArgumentObject:public GuardedNativeObject {
public:
    int conversionCalls=0;
    tjs_error FuncCall(tjs_uint32 flag,const tjs_char* name,tjs_uint32* hint,tTJSVariant* result,
            tjs_int count,tTJSVariant** params,iTJSDispatch2* object) override {
        // VM lifecycle finalization is independent of argument conversion.
        if(name && !std::strcmp(name,"finalize"))
            return tTJSCustomObject::FuncCall(flag,name,hint,result,count,params,object);
        ++conversionCalls;throw std::runtime_error("unused argument conversion invoked");
    }
    tjs_error PropGet(tjs_uint32,const tjs_char*,tjs_uint32*,tTJSVariant*,iTJSDispatch2*) override {
        ++propertyGets;throw std::runtime_error("unused argument getter invoked");
    }
};
int C2BDiagnosticProducerCaptures=0;
void C2BDiscardDiagnostic(const char*) {}
bool C2BUnknownDiagnosticIdentity(void*,krkrsdl3::cpu_consumer_trace::Producer&) {
    ++C2BDiagnosticProducerCaptures;return false;
}
int RunC2SpanBindingTests() {
    try {
        ncbClassInfo<LayerExDraw>::Set(TJS_N("C2BLayer"),0x421357,nullptr);
        DirectRegistration<Appearance> appearanceClass(TJS_N("C2BApp"));
        DirectRegistration<Path> pathClass(TJS_N("C2BPath"));
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
        auto appearance=ConstructDirect<Appearance>();
        auto* app=UnboxDirect<Appearance>(appearance);
        app->drawInfos.emplace_back();app->drawInfos.back().info=new SoftPen(0x9162a430u,4.3);app->drawInfos.back().type=0;
        app->drawInfos.emplace_back();app->drawInfos.back().info=new SoftBrush(0x635080a0u);app->drawInfos.back().type=1;
        auto pathArg=ConstructDirect<Path>();
        auto* path=UnboxDirect<Path>(pathArg);plutovg_path_add_ellipse(const_cast<plutovg_path_t*>(path->spanCapturePath()),30.2f,17.4f,18.3f,10.1f);
        {
            auto* boxedApp=new Appearance;
            auto boxed=BoxDirect(boxedApp);
            Check(UnboxDirect<Appearance>(boxed)==boxedApp,"default Appearance boxing/unboxing changed native type");
            auto* boxedPath=new Path;
            auto boxedP=BoxDirect(boxedPath);
            Check(UnboxDirect<Path>(boxedP)==boxedPath,"default Path boxing/unboxing changed native type");
        }
        auto* srcSurface=plutovg_surface_create(9,7);
        std::fill_n(reinterpret_cast<uint32_t*>(plutovg_surface_get_data(srcSurface)),63,0x80402010u);
        auto imageArg=BoxImage(new GdipImage(srcSurface));plutovg_surface_destroy(srcSurface);
        std::vector<std::pair<const char*,std::vector<tTJSVariant>>> operations={
            {"drawLine",{appearance,5.3,8.2,59.4,28.7}},
            {"drawPath",{appearance,pathArg}},
            {"drawImageStretch",{7.3,4.2,48.7,27.4,imageArg,-2.1,0.0,13.3,7.8}}};
        {
            using Policy=ncbInvocationPolicy<LayerExDraw>;
            Check(Policy::DirectNative<Appearance>(&appearance)==app && Policy::DirectNative<Path>(&pathArg)==path,
                "direct production native probe did not match registered argument");
            Check(Policy::ImageNative(&imageArg)!=nullptr && !Policy::ImageNative(&appearance),
                "image wrapper probe changed native type");
            LayerFixture target;
            auto reason=[&](const char* method,std::vector<tTJSVariant> values) {
                std::vector<tTJSVariant*> params;for(auto& v:values)params.push_back(&v);
                const char* value=Policy::Preflight(method,params.size(),params.data(),target.Draw());
                return std::string(value?value:"");
            };
            for(const auto& op:operations) Check(reason(op.first,op.second).empty(),"registered arguments failed preflight");
            Check(reason("drawLine",{})=="arguments","argument count reason changed");
            Check(reason("drawLine",{1,0.0,1.0,2.0,3.0})=="appearanceType","primitive Appearance admitted capture");
            Check(reason("drawPath",{appearance,tTJSVariant(static_cast<iTJSDispatch2*>(nullptr))})=="pathType",
                "null Path admitted capture");
            Check(reason("drawLine",{pathArg,0.0,1.0,2.0,3.0})=="appearanceType","Appearance type reason missing");
            Check(reason("drawPath",{appearance,appearance})=="pathType","Path type reason missing");
            auto imageValues=operations[2].second;imageValues[4]=appearance;
            Check(reason("drawImageStretch",imageValues)=="imageType","image type reason missing");
            auto lineValues=operations[0].second;lineValues[1]=TJS_N("5.3");
            Check(reason("drawLine",lineValues)=="numeric","numeric reason missing");
            lineValues[1]=std::numeric_limits<double>::infinity();
            Check(reason("drawLine",lineValues)=="numeric","nonfinite numeric probe admitted capture");
            auto malformedApp=ConstructDirect<Appearance>();auto* bad=UnboxDirect<Appearance>(malformedApp);
            bad->drawInfos.emplace_back();bad->drawInfos.back().type=3;bad->drawInfos.back().info=new SoftBrush(tjs_uint32(0));
            Check(reason("drawLine",{malformedApp,0.0,1.0,2.0,3.0})=="paint","paint reason missing");
            // Do not change legacy ownership handling while supplying a malformed paint.
            delete static_cast<SoftBrush*>(bad->drawInfos.back().info);bad->drawInfos.back().info=nullptr;
            auto malformedPath=ConstructDirect<Path>();auto* badPath=UnboxDirect<Path>(malformedPath);
            plutovg_path_move_to(const_cast<plutovg_path_t*>(badPath->spanCapturePath()),std::numeric_limits<float>::infinity(),0);
            Check(reason("drawPath",{appearance,malformedPath})=="path","path data reason missing");
            imageValues[4]=BoxImage(new GdipImage(9,7));
            Check(reason("drawImageStretch",imageValues)=="vectorSource","vector image reason missing");
            imageValues[4]=BoxImage(new GdipImage(static_cast<plutovg_surface_t*>(nullptr)));
            Check(reason("drawImageStretch",imageValues)=="source","missing image surface reason missing");
        }
        {
            LayerFixture cpu,gpu;auto values=operations[2].second;values[4]=TJS_N("oracle-image.png");
            ImageLoadSurface=srcSurface;const int before=ImageLoads,attempts=Attempts;
            Support=false;call(cpu,"drawImageStretch",values);
            Support=true;call(gpu,"drawImageStretch",values);ImageLoadSurface=nullptr;
            Check(ImageLoads==before+2 && Attempts==attempts && gpu.native->texture->counts->writes==1 &&
                cpu.native->texture->pixels==gpu.native->texture->pixels,"filename image conversion skipped eager CPU or executed twice");
        }
        {
            namespace trace=krkrsdl3::cpu_consumer_trace;
            namespace work=krkrsdl3::layer_work;
            struct RestoreDiagnostics {
                bool enabled=work::enabled.load(std::memory_order_relaxed);
                void (*logger)(const char*)=trace::logMessage;
                bool (*producer)(void*,trace::Producer&)=trace::captureProducer;
                void (*taken)(const work::CPUConsumerBudget&,uint64_t)=work::cpuConsumerWindowTaken;
                ~RestoreDiagnostics() {
                    work::SetEnabled(false);trace::SetCallbacks(logger,producer);work::cpuConsumerWindowTaken=taken;
                    if(enabled) work::SetEnabled(true);
                }
            } restore;
            trace::SetCallbacks(C2BDiscardDiagnostic,C2BUnknownDiagnosticIdentity);
            auto* unused=new UnusedArgumentObject;
            tTJSVariant unusedArgument(unused,unused);unused->Release();
            auto sameResult=[](const tTJSVariant& a,const tTJSVariant& b) {
                auto* x=ncbInstanceAdaptor<RectF>::GetNativeInstance(a.AsObjectNoAddRef());
                auto* y=ncbInstanceAdaptor<RectF>::GetNativeInstance(b.AsObjectNoAddRef());
                return x && y && x->x==y->x && x->y==y->y && x->w==y->w && x->h==y->h;
            };
            bool allExtraCallsGPU=true;
            for(const auto& op:operations) {
                LayerFixture baseline;tTJSVariant expected;Support=false;work::SetEnabled(false);
                Check(call(baseline,op.first,op.second,&expected)==TJS_S_OK,"normal arity CPU binding failed");
                for(int extraCount:{1,4}) {
                    auto values=op.second;values.push_back(unusedArgument);
                    if(extraCount>1) {values.push_back(TJS_N("unused nonnumeric"));
                        values.push_back(std::numeric_limits<double>::infinity());values.emplace_back();}
                    LayerFixture cpu,gpu;tTJSVariant cpuResult,gpuResult;
                    Support=false;work::SetEnabled(false);const int cpuConstructors=ResultConstructors;
                    Check(call(cpu,op.first,values,&cpuResult)==TJS_S_OK && ResultConstructors==cpuConstructors+1 &&
                        cpu.native->texture->pixels==baseline.native->texture->pixels && sameResult(cpuResult,expected),
                        "extra arguments changed legacy CPU pixels, result or boxing count");
                    Support=true;work::SetEnabled(true);const int attempts=Attempts,constructors=ResultConstructors;
                    Check(call(gpu,op.first,values,&gpuResult)==TJS_S_OK && ResultConstructors==constructors+1 &&
                        gpu.native->texture->pixels==baseline.native->texture->pixels && sameResult(gpuResult,expected),
                        "extra arguments changed bound pixels, typed result or boxing count");
                    krkrsdl3::span_route::Window window;
                    {std::lock_guard<std::mutex> lock(work::mutex);window=work::profile.cpuConsumerBudget.spanWindow;}
                    const bool captured=Attempts==attempts+1 && gpu.native->texture->counts->reads==0 &&
                        gpu.native->texture->counts->writes==0 && gpu.native->updates==baseline.native->updates &&
                        gpu.native->modified && window.totals.calls==1 && window.routes[0]==1 && window.routes[1]==0;
                    allExtraCallsGPU=allExtraCallsGPU && captured;
                    const size_t argumentGroup=(krkrsdl3::span_route::Find(krkrsdl3::span_route::Methods,op.first)*
                        krkrsdl3::span_route::Routes.size()+krkrsdl3::span_route::Find(krkrsdl3::span_route::Routes,"cpu"))*
                        krkrsdl3::span_route::Reasons.size()+krkrsdl3::span_route::Find(krkrsdl3::span_route::Reasons,"arguments");
                    std::cout<<"C2B extra arity method="<<op.first<<" required="<<op.second.size()<<" passed="<<values.size()
                        <<" gpu="<<window.routes[0]<<" cpu="<<window.routes[1]<<" cpuArguments="<<window.groups[argumentGroup].metrics.calls
                        <<" attempts="<<Attempts-attempts<<" cpuWrites="<<gpu.native->texture->counts->writes<<'\n';
                    work::Take();
                    if(captured) {
                        LayerFixture rejected;tTJSVariant rejectedResult;RejectSubmit=true;
                        const int rejectAttempts=Attempts,rejectConstructors=ResultConstructors;
                        const auto hr=call(rejected,op.first,values,&rejectedResult);RejectSubmit=false;
                        Check(hr==TJS_S_OK && Attempts==rejectAttempts+1 && ResultConstructors==rejectConstructors+1 &&
                            rejected.native->texture->counts->writes==1 && rejected.native->texture->counts->reads==0 &&
                            rejected.native->updates==baseline.native->updates && rejected.native->texture->pixels==baseline.native->texture->pixels &&
                            sameResult(rejectedResult,expected),"extra arity rejection duplicated CPU replay/result boxing or changed pixels");
                    }
                    Check(unused->nativeGets==0 && unused->propertyGets==0 && unused->conversionCalls==0,
                        "unused extra argument triggered native conversion or getter");
                }
                for(int supported=0;supported<2;++supported) {
                    LayerFixture shortCall;Support=supported!=0;auto values=op.second;values.pop_back();
                    tTJSVariant result(123);const auto pixels=shortCall.native->texture->pixels;
                    const int attempts=Attempts,constructors=ResultConstructors;
                    Check(call(shortCall,op.first,values,&result)==TJS_E_BADPARAMCOUNT && result.Type()==tvtInteger && result.AsInteger()==123 &&
                        Attempts==attempts && ResultConstructors==constructors && shortCall.native->texture->counts->reads==0 &&
                        shortCall.native->texture->counts->writes==0 && shortCall.native->updates==0 && !shortCall.native->modified &&
                        shortCall.native->texture->pixels==pixels,"too few arguments changed legacy error, eager access or result");
                }
            }
            Check(allExtraCallsGPU,"NCBind accepts unused extra arguments but GPU preflight incorrectly rejects them");
            std::cout<<"PASS C2B minimum arity: three methods accept one/multiple untouched extra arguments; exact pixels/results, zero CPU acquire, sole replay/boxing, unchanged short-call errors\n";
        }
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
            namespace trace=krkrsdl3::cpu_consumer_trace;
            namespace work=krkrsdl3::layer_work;
            struct RestoreDiagnostics {
                bool enabled=work::enabled.load(std::memory_order_relaxed);
                void (*logger)(const char*)=trace::logMessage;
                bool (*producer)(void*,trace::Producer&)=trace::captureProducer;
                void (*taken)(const work::CPUConsumerBudget&,uint64_t)=work::cpuConsumerWindowTaken;
                ~RestoreDiagnostics() {
                    work::SetEnabled(false);trace::SetCallbacks(logger,producer);work::cpuConsumerWindowTaken=taken;
                    if(enabled) work::SetEnabled(true);
                }
            } restore;
            trace::SetCallbacks(C2BDiscardDiagnostic,C2BUnknownDiagnosticIdentity);
            auto window=[] {
                std::lock_guard<std::mutex> lock(work::mutex);
                return work::profile.cpuConsumerBudget.spanWindow;
            };
            for(const auto& op:operations) {
                LayerFixture off,on;tTJSVariant offResult,onResult;Support=true;
                work::SetEnabled(false);const int offAttempts=Attempts,offConstructors=ResultConstructors;
                call(off,op.first,op.second,&offResult);
                Check(Attempts==offAttempts+1 && ResultConstructors==offConstructors+1 && window().totals.calls==0,
                    "disabled diagnostics changed submission/boxing or recorded span work");
                const auto offProfile=work::Take();
                work::SetEnabled(true);C2BDiagnosticProducerCaptures=0;
                const int onAttempts=Attempts,onConstructors=ResultConstructors;
                call(on,op.first,op.second,&onResult);
                Check(Attempts==onAttempts+1 && ResultConstructors==onConstructors+1 &&
                    off.native->texture->pixels==on.native->texture->pixels &&
                    off.native->texture->counts->reads==on.native->texture->counts->reads &&
                    off.native->texture->counts->writes==on.native->texture->counts->writes &&
                    off.native->texture->counts->unlocks==on.native->texture->counts->unlocks &&
                    off.native->texture->counts->dirty==on.native->texture->counts->dirty &&
                    off.native->updates==on.native->updates && off.native->modified==on.native->modified,
                    "diagnostics changed bound GPU pixels, acquisitions, updates, submission or boxing");
                Check(on.native->texture->counts->reads==0 && on.native->texture->counts->writes==0,
                    "enabled diagnostics acquired target CPU pixels");
                auto* offRect=ncbInstanceAdaptor<RectF>::GetNativeInstance(offResult.AsObjectNoAddRef());
                auto* onRect=ncbInstanceAdaptor<RectF>::GetNativeInstance(onResult.AsObjectNoAddRef());
                Check(offRect && onRect && offRect->x==onRect->x && offRect->y==onRect->y &&
                    offRect->w==onRect->w && offRect->h==onRect->h,"diagnostics changed typed bound result");
                const auto recorded=window();
                const auto method=krkrsdl3::span_route::Find(krkrsdl3::span_route::Methods,op.first);
                const auto group=(method*krkrsdl3::span_route::Routes.size())*krkrsdl3::span_route::Reasons.size();
                size_t groups=0,samples=0;
                for(const auto& value:recorded.groups) if(value.metrics.calls) ++groups;
                for(const auto& value:recorded.samples) if(value.used) {
                    ++samples;Check(value.group==group && value.metrics.calls==1 && value.traceID!=0,
                        "enabled binding sampled wrong method/group or repeated call");
                }
                Check(recorded.totals.calls==1 && recorded.routes[0]==1 && recorded.routes[1]==0 && recorded.routes[2]==0 &&
                    recorded.groups[group].metrics.calls==1 && groups==1 && samples==1 && C2BDiagnosticProducerCaptures==1 &&
                    recorded.repeatedOmitted==0 && recorded.capacityOmitted==0 && recorded.invalidRecords==0 && !recorded.overflow,
                    "enabled binding did not record exactly one GPU aggregate and representative");
                const auto onProfile=work::Take();const auto reset=window();
                Check(offProfile.transfers.empty() && onProfile.transfers.empty() &&
                    offProfile.transferOrigins.empty() && onProfile.transferOrigins.empty(),
                    "binding route diagnostics changed C0 transfer totals");
                Check(onProfile.spanRouteWindowID==recorded.id && reset.id>recorded.id && reset.totals.calls==0,
                    "binding diagnostic take did not reset window counts");
                const auto emptyProfile=work::Take();const auto empty=window();
                for(const auto& value:empty.samples) Check(!value.used,"empty take replayed binding representative");
                Check(empty.totals.calls==0 && empty.routes[0]==0 && empty.routes[1]==0 && empty.routes[2]==0 &&
                    emptyProfile.transfers.empty() && C2BDiagnosticProducerCaptures==1,
                    "empty take repeated bound route counts or identity capture");
            }
            std::cout<<"PASS C2B diagnostics on/off: three bound methods preserve exact pixels, typed results, submission/update/acquisition counts; one GPU group/sample, no C0 transfers or repeats\n";
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
            auto empty=ConstructDirect<Appearance>();
            call(target,"drawLine",{empty,2.0,4.0,40.0,16.0});
            Check(Attempts==attempts && target.native->texture->counts->writes==0 && target.native->updates==1 && target.native->modified,
                "empty span noop acquired CPU or missed legacy Update/ImageModified");
        }
        {
            LayerFixture target;Support=true;const int attempts=Attempts;
            std::vector<uint32_t> borrowedPixels(63,0x80402010u);
            auto* borrowed=plutovg_surface_create_for_data(reinterpret_cast<unsigned char*>(borrowedPixels.data()),9,7,36);
            auto borrowedArg=BoxImage(new GdipImage(borrowed));plutovg_surface_destroy(borrowed);
            call(target,"drawImageStretch",{7.3,4.2,48.7,27.4,borrowedArg,0.0,0.0,9.0,7.0});
            Check(Attempts==attempts && target.native->texture->counts->writes==1,"borrowed image source skipped CPU acquisition");
            std::vector<tTJSVariant> values={7.3,4.2,48.7,27.4,borrowedArg,0.0,0.0,9.0,7.0};
            std::vector<tTJSVariant*> params;for(auto& v:values)params.push_back(&v);
            Check(!std::strcmp(ncbInvocationPolicy<LayerExDraw>::Preflight("drawImageStretch",9,params.data(),target.Draw()),"borrowedSource"),
                "borrowed source route reason missing");
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
            LayerFixture cpu;Support=false;call(cpu,"drawLine",values);Support=true;
            Check(cpu.native->texture->pixels==target.native->texture->pixels,"numeric fallback changed legacy pixels");
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
        {
            // Failed speculative probes preserve the sole eager legacy lease
            // and its original throwing unbox conversion.
            for(const char* method:{"drawLine","drawPath"}) {
                auto values=operations[!std::strcmp(method,"drawLine")?0:1].second;values[0]=pathArg;
                std::string errors[2];
                for(int mode=0;mode<2;++mode) {
                    LayerFixture target;Support=mode!=0;const int attempts=Attempts;
                    try {call(target,method,values);}catch(const std::exception& error){errors[mode]=error.what();}
                    Check(target.native->texture->counts->writes==1 && target.native->texture->counts->reads==0 && Attempts==attempts,
                        "malformed Appearance changed eager lease or reached submission");
                }
                Check(!errors[0].empty() && errors[0]==errors[1],"malformed Appearance changed CPU conversion error");
            }
            LayerFixture target;Support=true;const int attempts=Attempts;
            bool threw=false;try {call(target,"drawPath",{appearance,appearance});}catch(...){threw=true;}
            Check(threw && Attempts==attempts && target.native->texture->counts->writes==1,
                "malformed Path changed eager legacy conversion");
        }
        {
            auto* object=new GuardedNativeObject;
            Check(ncbInstanceAdaptor<Appearance>::SetAdaptorWithNativeInstance(object,app->Clone()),"guarded Appearance setup failed");
            tTJSVariant guarded(object,object);object->Release();object->nativeGets=object->propertyGets=0;
            Check(!ncbInvocationPolicy<LayerExDraw>::DirectNative<Appearance>(&guarded) && object->nativeGets==0 && object->propertyGets==0,
                "speculative native probe observed guarded dispatch");
            LayerFixture target;Support=true;const int attempts=Attempts;
            auto values=operations[0].second;values[0]=guarded;call(target,"drawLine",values);
            Check(Attempts==attempts && target.native->texture->counts->writes==1 && object->nativeGets==1 && object->propertyGets==0,
                "guarded argument skipped eager lease or repeated legacy conversion/getter");
        }
        std::cout<<"C2B real plutovg production binding transaction checks passed\n";Support=false;return 0;
    }catch(const std::exception& error){std::cerr<<"C2B binding: "<<error.what()<<'\n';return 1;}
}
