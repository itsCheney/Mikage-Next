#include "tjsCommHead.h"
#include "tjsNative.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "CPUConsumerTrace.h"
#include "ncbind/ncbind.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>
#include <optional>

// The raw-first oracle never enters GdipImage boxing (there is no Draw/record).
// Supply the platform hook required to link that untouched callback branch.
iTJSDispatch2* TVPGetScriptDispatch() { return nullptr; }

// The VM, NCBind dispatch/policy and scoped texture lease are production code.
// This canvas double checks boundary ordering and historical clip intersection;
// it does not claim to verify plutovg's native rasterization or GPU pixels.
static void RequireDraw(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct TestDrawClass { static constexpr tjs_int32 ClassID=0x213456; };
struct TestDrawCounters { int reads=0,writes=0,unlocks=0,dirty=0,destroyed=0; };
class TestDrawTexture : public iTVPTexture2D {
public:
    std::shared_ptr<TestDrawCounters> counters=std::make_shared<TestDrawCounters>();
    std::vector<uint32_t> pixels;
    bool stable=true,fail=false;
    uint64_t identity;
    tTVPRect lastDirty;
    static uint64_t nextID;
    TestDrawTexture(int w,int h,bool key=true):iTVPTexture2D(w,h),pixels(size_t(w)*h,0x80402010),stable(key),identity(++nextID) {}
    ~TestDrawTexture() override { ++counters->destroyed; }
    TVPTextureFormat::e GetFormat() const override { return TVPTextureFormat::RGBA; }
    int GetPitch() const override { return Width*4; }
    void Update(const void*,TVPTextureFormat::e,int,const tTVPRect&) override {}
    uint32_t GetPoint(int x,int y) override { return pixels[size_t(y)*Width+x]; }
    void SetPoint(int x,int y,uint32_t c) override { pixels[size_t(y)*Width+x]=c; }
    bool IsStatic() override { return false; }
    bool IsOpaque() override { return false; }
    bool GetTextureData(void*,int&) override { return false; }
    bool GetContentKey(uint64_t& id,uint64_t& version) const override { id=identity;version=counters->dirty;return stable; }
    void* LockCPURead() override { ++counters->reads;return pixels.data(); }
    void* LockCPUWrite() override { ++counters->writes;if(fail) throw std::runtime_error("lease failure");return pixels.data(); }
    void UnlockCPU() override { ++counters->unlocks; }
    void UnlockCPUWrite(const tTVPRect& rect) override { ++counters->dirty;lastDirty=rect;UnlockCPU(); }
};
uint64_t TestDrawTexture::nextID=0;
class TestDrawNativeLayer : public tTJSNativeInstance {
public:
    uint64_t GetLayerDiagnosticID() const { return 100; }
    TestDrawTexture* texture;
    int updates=0,cow=0;
    tTVPRect clip;
    explicit TestDrawNativeLayer(TestDrawTexture* value):texture(value),clip(0,0,value->GetWidth(),value->GetHeight()) {}
    ~TestDrawNativeLayer() override { texture->Release(); }
    int GetImageWidth() { return texture->GetWidth(); }
    int GetImageHeight() { return texture->GetHeight(); }
    int GetClipLeft() { return clip.left; }
    int GetClipTop() { return clip.top; }
    int GetClipWidth() { return clip.get_width(); }
    int GetClipHeight() { return clip.get_height(); }
    void Update() { ++updates; }
    void Update(const tTVPRect&) { ++updates; }
    iTVPTexture2D* GetMainImageTextureForCPUAccess(bool write) {
        if(write && !texture->IsIndependent()) {
            auto* replacement=new TestDrawTexture(GetImageWidth(),GetImageHeight(),texture->stable);
            replacement->pixels=texture->pixels;texture->Release();texture=replacement;++cow;
        }
        return texture;
    }
    void Replace(TestDrawTexture* value) { texture->Release();texture=value;clip=tTVPRect(0,0,value->GetWidth(),value->GetHeight()); }
};

struct plutovg_matrix_t { float a=1,b=0,c=0,d=1,e=0,f=0; };
static void plutovg_matrix_init_identity(plutovg_matrix_t* m) { *m=plutovg_matrix_t{}; }
static void plutovg_matrix_init(plutovg_matrix_t* m,float a,float b,float c,float d,float e,float f) { *m={a,b,c,d,e,f}; }
static void plutovg_matrix_multiply(plutovg_matrix_t* out,const plutovg_matrix_t* a,const plutovg_matrix_t* b) { *out=*a;out->e+=b->e;out->f+=b->f; }
static void plutovg_matrix_init_rotate(plutovg_matrix_t* m,float value) { *m={};m->b=value; }
static void plutovg_matrix_init_scale(plutovg_matrix_t* m,float x,float y) { *m={};m->a=x;m->d=y; }
static void plutovg_matrix_init_translate(plutovg_matrix_t* m,float x,float y) { *m={};m->e=x;m->f=y; }
struct plutovg_surface_t { unsigned char* data;int width,height,pitch,refs=1; };
struct plutovg_canvas_t { plutovg_surface_t* surface;tTVPRect clip;uint32_t color=0; };
struct plutovg_path_t {};
struct plutovg_font_face_t {};
struct plutovg_rect_t { float x,y,w,h; };
struct plutovg_color_t { uint32_t argb; };
static constexpr int PLUTOVG_OPERATOR_SRC_OVER=0,PLUTOVG_OPERATOR_SRC=1,PLUTOVG_TEXTURE_TYPE_PLAIN=0,PLUTOVG_TEXT_ENCODING_UTF8=0;
static int TestCanvasCreates=0;
static bool TestThrowFill=false;
static plutovg_surface_t* plutovg_surface_create_for_data(unsigned char* d,int w,int h,int p) { return new plutovg_surface_t{d,w,h,p}; }
static void plutovg_surface_destroy(plutovg_surface_t* s) { if(!--s->refs) delete s; }
static plutovg_canvas_t* plutovg_canvas_create(plutovg_surface_t* s) { ++TestCanvasCreates;++s->refs;return new plutovg_canvas_t{s,tTVPRect(0,0,s->width,s->height)}; }
static void plutovg_canvas_destroy(plutovg_canvas_t* c) { plutovg_surface_destroy(c->surface);delete c; }
static void plutovg_canvas_set_operator(plutovg_canvas_t*,int) {}
static void plutovg_canvas_save(plutovg_canvas_t*) {}
static void plutovg_canvas_restore(plutovg_canvas_t*) {}
static void plutovg_canvas_reset_matrix(plutovg_canvas_t*) {}
static void plutovg_canvas_set_matrix(plutovg_canvas_t*,const plutovg_matrix_t*) {}
static void plutovg_canvas_set_texture(plutovg_canvas_t*,plutovg_surface_t*,int,float,const plutovg_matrix_t*) {}
static void plutovg_color_init_argb32(plutovg_color_t* c,uint32_t v) { c->argb=v; }
static void plutovg_canvas_set_color(plutovg_canvas_t* c,const plutovg_color_t* v) { c->color=v->argb; }
static void plutovg_canvas_clip_rect(plutovg_canvas_t* c,float x,float y,float w,float h) {
    c->clip.left=std::max(c->clip.left,int(x));c->clip.top=std::max(c->clip.top,int(y));
    c->clip.right=std::min(c->clip.right,int(x+w));c->clip.bottom=std::min(c->clip.bottom,int(y+h));
}
static void plutovg_canvas_fill_rect(plutovg_canvas_t* c,float,float,float,float) {
    if(TestThrowFill) throw std::runtime_error("canvas failure");
    for(int y=c->clip.top;y<c->clip.bottom;++y) for(int x=c->clip.left;x<c->clip.right;++x)
        std::memcpy(c->surface->data+size_t(y)*c->surface->pitch+x*4,&c->color,4);
}
static bool plutovg_surface_write_to_png(plutovg_surface_t*,const tjs_char*) { return true; }
static void plutovg_font_face_text_extents(plutovg_font_face_t*,float,const tjs_char*,int length,int,plutovg_rect_t* r) { *r={0,0,float(length)*5,10}; }
enum SmoothingMode { SmoothingModeAntiAlias=4 };
enum TextRenderingHint { TextRenderingHintAntiAlias=4 };
struct RectF { tjs_real x=0,y=0,w=0,h=0;RectF()=default;RectF(tjs_real X,tjs_real Y,tjs_real W,tjs_real H):x(X),y(Y),w(W),h(H) {} };
struct PointF {};
struct Appearance {};
struct Path {};
struct SoftPen {};
struct SoftBrush {};
struct GdipMatrix {
    plutovg_matrix_t _core;
    GdipMatrix()=default;explicit GdipMatrix(plutovg_matrix_t value):_core(value) {}
    GdipMatrix* Clone() const { return new GdipMatrix(_core); }
};
struct FontInfo {
    plutovg_font_face_t face;
    plutovg_font_face_t* getFontFace() const { return const_cast<plutovg_font_face_t*>(&face); }
    float getEmSize() const { return 10; }
};
struct GdipImage {
    int width,height;uint32_t bgColor=0;plutovg_surface_t* _surface=nullptr;
    GdipImage(int w,int h):width(w),height(h) {}
    explicit GdipImage(plutovg_surface_t* s):width(s?s->width:0),height(s?s->height:0),_surface(s) { if(s) ++s->refs; }
    ~GdipImage() { if(_surface) plutovg_surface_destroy(_surface); }
    GdipImage* Clone() { return new GdipImage(width,height); }
    int GetWidth() { return width; }
};

#include "ProductionLayerExBoundary.inc"

struct TestDrawObject {
    iTJSDispatch2* object=new tTJSCustomObject;
    TestDrawNativeLayer* native;
    TestDrawObject(int w=4,int h=3,bool stable=true):native(new TestDrawNativeLayer(new TestDrawTexture(w,h,stable))) {
        iTJSNativeInstance* instance=native;
        object->NativeInstanceSupport(TJS_NIS_REGISTER,TestDrawClass::ClassID,&instance);
    }
    ~TestDrawObject() { object->Invalidate(0,nullptr,nullptr,object);object->Release(); }
    LayerExDraw* Draw() { return ncbInvocationPolicy<LayerExDraw>::Scope::Instance(object); }
};
struct TestDrawRegistration : ncbRegistNativeClassBase {
    using NativeClassT=LayerExDraw;
    std::map<std::string,iTJSDispatch2*> methods;
    TestDrawRegistration():ncbRegistNativeClassBase(TJS_N("LayerExDraw")) {}
    void RegistItem(NameT name,ItemT item) { methods[ttstr(name).AsStdString()]=item->GetDispatch();item->Release(); }
    ~TestDrawRegistration() { for(auto& entry:methods) entry.second->Release(); }
};
static tjs_error TestDrawRaw(tTJSVariant*,tjs_int,tTJSVariant**,iTJSDispatch2* object) {
    std::unique_ptr<GdipImage> image(ncbInvocationPolicy<LayerExDraw>::Scope::Instance(object)->getRecordImage());return TJS_S_OK;
}
static tjs_error TestDrawTypedRaw(tTJSVariant*,tjs_int,tTJSVariant**,LayerExDraw* draw) {
    std::unique_ptr<GdipImage> image(draw->getRecordImage());return TJS_S_OK;
}
struct TestDrawBridge {
    static std::unique_ptr<GdipImage> image;
    GdipImage* operator()(LayerExDraw* draw) const { image.reset((GdipImage*)*draw);return image.get(); }
};
std::unique_ptr<GdipImage> TestDrawBridge::image;
static std::function<void()> TestMatrixGetter;
static tjs_error TestMatrixGet(tTJSVariant* result,iTJSDispatch2*) { if(TestMatrixGetter) TestMatrixGetter();*result=1;return TJS_S_OK; }
static tjs_error TestMatrixDeny(const tTJSVariant*,iTJSDispatch2*) { return TJS_E_ACCESSDENYED; }
static iTJSDispatch2* MatrixObject() {
    auto* object=new tTJSCustomObject;
    for(const char* name:{"m12","m21","m22","dx","dy"}) {
        tTJSVariant value(0);object->PropSet(TJS_MEMBERENSURE,name,nullptr,&value,object);
    }
    auto* getter=TJSCreateNativeClassProperty(TestMatrixGet,TestMatrixDeny);
    tTJSVariant property(getter,object);object->PropSet(TJS_MEMBERENSURE|TJS_IGNOREPROP,TJS_N("m11"),nullptr,&property,object);
    getter->Release();return object;
}

int RunC2LayerExBoundaryTests() {
    try {
        ncbClassInfo<LayerExDraw>::Set(TJS_N("LayerExDraw"),0x313456,nullptr);
        TestDrawRegistration registration;
        {
            ncbRegistClass<TestDrawRegistration> reg(registration,true);
            reg.Method(TJS_N("clear"),&LayerExDraw::clear);
            reg.Method(TJS_N("translateViewTransform"),&LayerExDraw::translateViewTransform);
            reg.Method(TJS_N("setViewTransform"),&LayerExDraw::setViewTransform);
            reg.Method(TJS_N("setTransform"),&LayerExDraw::setTransform);
            reg.Method(TJS_N("resetViewTransform"),&LayerExDraw::resetViewTransform);
            reg.Method(TJS_N("rotateViewTransform"),&LayerExDraw::rotateViewTransform);
            reg.Method(TJS_N("scaleViewTransform"),&LayerExDraw::scaleViewTransform);
            reg.Method(TJS_N("translateTransform"),&LayerExDraw::translateTransform);
            reg.Method(TJS_N("resetTransform"),&LayerExDraw::resetTransform);
            reg.Method(TJS_N("rotateTransform"),&LayerExDraw::rotateTransform);
            reg.Method(TJS_N("scaleTransform"),&LayerExDraw::scaleTransform);
            reg.Method(TJS_N("saveRecord"),&LayerExDraw::saveRecord);
            reg.Property(TJS_N("record"),&LayerExDraw::getRecord,&LayerExDraw::setRecord);
            reg.Property(TJS_N("unknownRO"),&LayerExDraw::getRecord,(int)0);
            reg.Property(TJS_N("smoothingMode"),&LayerExDraw::getSmoothingMode,&LayerExDraw::setSmoothingMode);
            reg.Property(TJS_N("updateWhenDraw"),&LayerExDraw::getUpdateWhenDraw,&LayerExDraw::setUpdateWhenDraw);
            reg.Property(TJS_N("textRenderingHint"),&LayerExDraw::getTextRenderingHint,&LayerExDraw::setTextRenderingHint);
            reg.RawCallback(TJS_N("getRecordImage"),TestDrawRaw,0);
            reg.RawCallback(TJS_N("productionRecordImage"),GetRecordImage,0);
            reg.RawCallback(TJS_N("typedRecordImage"),TestDrawTypedRaw,0);
            reg.RawCallback(TJS_N("rawRecord"),TestDrawRaw,TestDrawRaw,0);
            reg.RawCallback(TJS_N("rawRO"),TestDrawRaw,(int)0,0);
            reg.Method(TJS_N("unknownEntry"),&LayerExDraw::getRecord);
            reg.Method(TJS_N("imageWidth"),&GdipImage::GetWidth,
                ncbNativeClassMethodBase::InvokeType::ivtBridge<decltype(&TestDrawBridge::operator())>());
        }
        auto call=[&](TestDrawObject& fixture,const char* name,std::vector<tTJSVariant> values) {
            std::vector<tTJSVariant*> args;for(auto& value:values) args.push_back(&value);
            return registration.methods.at(name)->FuncCall(0,nullptr,nullptr,nullptr,args.size(),args.data(),fixture.object);
        };
        {
            TestDrawObject baselineFresh,rawFresh,typedFresh,bridgeFresh;
            auto* baselineItem=ncbRawCallbackMethod<tTJSNativeClassMethodCallback>::Create(GetRecordImage,0);
            auto* baselineRaw=baselineItem->GetDispatch();baselineItem->Release();
            std::string baselineError,wrappedError;
            tTJSVariant baselineResult(7),wrappedResult(7);
            try { baselineRaw->FuncCall(0,nullptr,nullptr,&baselineResult,0,nullptr,baselineFresh.object); }
            catch(const std::exception& error) { baselineError=error.what(); }
            try { registration.methods.at("productionRecordImage")->FuncCall(0,nullptr,nullptr,&wrappedResult,0,nullptr,rawFresh.object); }
            catch(const std::exception& error) { wrappedError=error.what(); }
            RequireDraw(!baselineError.empty() && wrappedError==baselineError && wrappedResult.Type()==baselineResult.Type() &&
                !ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(rawFresh.object) && rawFresh.native->texture->counters->writes==0,
                "raw-first dispatch created Draw or changed original missing-instance error");
            RequireDraw(call(typedFresh,"typedRecordImage",{})==TJS_E_NATIVECLASSCRASH &&
                !ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(typedFresh.object) && typedFresh.native->texture->counters->writes==0,
                "typed raw-first dispatch created Draw or changed missing-instance rejection");
            RequireDraw(call(bridgeFresh,"imageWidth",{})==TJS_E_NATIVECLASSCRASH &&
                !ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(bridgeFresh.object) && bridgeFresh.native->texture->counters->writes==0,
                "bridge-first dispatch created Draw instead of original getter rejection");
            TestDrawObject emptyAdaptor;
            iTJSNativeInstance* empty=ncbInstanceAdaptor<LayerExDraw>::CreateEmptyAdaptor();
            emptyAdaptor.object->NativeInstanceSupport(TJS_NIS_REGISTER,ncbClassInfo<LayerExDraw>::GetID(),&empty);
            RequireDraw(call(emptyAdaptor,"productionRecordImage",{})==TJS_S_OK &&
                !ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(emptyAdaptor.object) && emptyAdaptor.native->texture->counters->writes==0,
                "raw empty-adaptor path created Draw instead of returning original empty result");
            RequireDraw(registration.methods.at("productionRecordImage")->FuncCall(0,TJS_N("missing"),nullptr,nullptr,0,nullptr,rawFresh.object)==
                baselineRaw->FuncCall(0,TJS_N("missing"),nullptr,nullptr,0,nullptr,baselineFresh.object) &&
                rawFresh.native->texture->counters->writes==0,"membername forwarding acquired pixels or changed error");
            RequireDraw(registration.methods.at("productionRecordImage")->FuncCall(0,nullptr,nullptr,nullptr,0,nullptr,nullptr)==
                baselineRaw->FuncCall(0,nullptr,nullptr,nullptr,0,nullptr,nullptr),"null raw objthis rejection changed");
            baselineRaw->Release();
        }
        {
            TestDrawObject firstPure;
            tTJSVariant firstResult;
            registration.methods.at("smoothingMode")->PropGet(0,nullptr,nullptr,&firstResult,firstPure.object);
            RequireDraw(firstPure.native->texture->counters->writes==0 && firstPure.native->texture->counters->dirty==0,
                "first pure dispatch constructed writable Draw facade");
            TestDrawObject firstWrite;
            call(firstWrite,"clear",{21});
            RequireDraw(firstWrite.native->texture->counters->writes==1 && firstWrite.native->texture->counters->unlocks==1 &&
                firstWrite.native->texture->pixels.back()==21,"first draw dispatch reset/acquired twice");
            auto item=ncbRawCallbackMethod<tTJSNativeClassMethodCallback>::Create(TestDrawRaw,0);
            auto legacy=ncbPolicyDispatch<FontInfo>::Wrap(TJS_N("legacy"),item,ncbInvocationKind::Raw);
            RequireDraw(legacy==item,"unrelated NCBind class received invocation policy wrapper");
            auto* dispatch=item->GetDispatch();item->Release();dispatch->Release();
        }
        TestDrawObject fixture;
        auto* texture=fixture.native->texture;auto counts=texture->counters;
        LayerExDraw* draw=fixture.Draw();
        RequireDraw(counts->writes==0 && counts->dirty==0,"Draw construction acquired pixels");
        RequireDraw(call(fixture,"clear",{})==TJS_E_BADPARAMCOUNT && counts->writes==0,
            "invalid method argument count acquired pixels");
        {
            TestDrawObject legacy;
            layerExBase_GL eager(legacy.object);
            RequireDraw(legacy.native->texture->counters->writes==1,"default extension construction lost legacy access");
        }
        tTJSVariant enabled(1),mode(4),result;
        RequireDraw(registration.methods.at("unknownRO")->PropSet(0,nullptr,nullptr,&enabled,fixture.object)==TJS_E_ACCESSDENYED &&
            counts->writes==0,"denied property setter acquired pixels");
        RequireDraw(registration.methods.at("unknownRO")->PropSet(0,nullptr,nullptr,nullptr,nullptr)==TJS_E_ACCESSDENYED &&
            registration.methods.at("rawRO")->PropSet(0,nullptr,nullptr,nullptr,nullptr)==TJS_E_ACCESSDENYED,
            "denied setter/null objthis error ordering changed");
        registration.methods.at("record")->FuncCall(0,nullptr,nullptr,nullptr,0,nullptr,fixture.object);
        registration.methods.at("clear")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        tTJSVariant attribute(23);
        auto* forwardingItem=ncbRawCallbackMethod<tTJSNativeClassMethodCallback>::Create(TestDrawRaw,0);
        auto* forwarding=forwardingItem->GetDispatch();forwardingItem->Release();
        RequireDraw(registration.methods.at("clear")->PropSet(TJS_MEMBERENSURE,TJS_N("attribute"),nullptr,&attribute,fixture.object)==
            forwarding->PropSet(TJS_MEMBERENSURE,TJS_N("attribute"),nullptr,&attribute,fixture.object) &&
            registration.methods.at("clear")->PropGet(0,TJS_N("attribute"),nullptr,&result,fixture.object)==
            forwarding->PropGet(0,TJS_N("attribute"),nullptr,&result,fixture.object) && counts->writes==0,
            "non-call/member attribute forwarding acquired pixels or changed error");
        forwarding->Release();
        registration.methods.at("smoothingMode")->PropSet(0,nullptr,nullptr,&mode,fixture.object);
        registration.methods.at("smoothingMode")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        registration.methods.at("updateWhenDraw")->PropSet(0,nullptr,nullptr,&enabled,fixture.object);
        registration.methods.at("updateWhenDraw")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        registration.methods.at("textRenderingHint")->PropSet(0,nullptr,nullptr,&mode,fixture.object);
        registration.methods.at("textRenderingHint")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        FontInfo font;
        {
            ncbInvocationPolicy<LayerExDraw>::Scope scope(TJS_N("measureString"),ncbInvocationKind::Method,fixture.object);
            RequireDraw(draw->measureString(&font,TJS_N("abc")).w==15,"measurement result changed");
        }
        {
            ncbInvocationPolicy<LayerExDraw>::Scope scope(TJS_N("measureStringInternal"),ncbInvocationKind::Method,fixture.object);
            draw->measureStringInternal(&font,TJS_N("abc"));
        }
        draw->setUpdateWhenDraw(0);draw->setTextRenderingHint(1);
        call(fixture,"translateViewTransform",{1,2});
        call(fixture,"resetViewTransform",{});call(fixture,"rotateViewTransform",{3});call(fixture,"scaleViewTransform",{1,1});
        call(fixture,"translateTransform",{1,2});call(fixture,"resetTransform",{});
        call(fixture,"rotateTransform",{3});call(fixture,"scaleTransform",{1,1});
        {
            GdipMatrix matrix;
            ncbInvocationPolicy<LayerExDraw>::Scope scope(TJS_N("setViewTransform"),ncbInvocationKind::Method,fixture.object);
            draw->setViewTransform(&matrix);
        }
        {
            GdipMatrix matrix;
            ncbInvocationPolicy<LayerExDraw>::Scope scope(TJS_N("setTransform"),ncbInvocationKind::Method,fixture.object);
            draw->setTransform(&matrix);
        }
        RequireDraw(counts->writes==0 && counts->dirty==0 && fixture.native->updates==0,"pure calls acquired/marked/updated pixels");
        registration.methods.at("record")->PropSet(0,nullptr,nullptr,&enabled,fixture.object);
        std::unique_ptr<GdipImage> record;
        { ncbInvocationPolicy<LayerExDraw>::Scope scope(TJS_N("getRecordImage"),ncbInvocationKind::Method,fixture.object);record.reset(draw->getRecordImage()); }
        RequireDraw(record->width==4 && record->height==3,"record allocated from unbound canvas geometry");
        RequireDraw(counts->writes==1 && counts->unlocks==1,"record redraw reacquired nested lease");
        const int before=counts->writes;
        call(fixture,"translateViewTransform",{2,3});
        RequireDraw(counts->writes==before+1 && counts->unlocks==counts->writes,"active record transform lease mismatch");
        call(fixture,"getRecordImage",{});
        call(fixture,"saveRecord",{tTJSVariant(TJS_N("double.png"))});
        RequireDraw(counts->writes==before+3 && counts->unlocks==counts->writes,"raw/save record nested lease mismatch");
        call(fixture,"typedRecordImage",{});
        registration.methods.at("rawRecord")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        registration.methods.at("rawRecord")->PropSet(0,nullptr,nullptr,&enabled,fixture.object);
        call(fixture,"unknownEntry",{});
        call(fixture,"imageWidth",{});
        RequireDraw(counts->writes==before+8 && counts->unlocks==counts->writes,
            "typed/raw property/unknown/bridge invocation bypassed conservative access");
        TestDrawBridge::image.reset();
        const int normalUpdates=fixture.native->updates,normalWrites=counts->writes;
        call(fixture,"rotateTransform",{4});
        RequireDraw(counts->writes==normalWrites && fixture.native->updates==normalUpdates,
            "pure recorded transform acquired pixels or changed redraw behavior");
        fixture.native->clip=tTVPRect(1,0,3,2);
        call(fixture,"clear",{tTJSVariant(tjs_int64(0x80abcdef))});
        RequireDraw(texture->pixels[0]==0 && texture->pixels[1]==0x80abcdef,"initial clip/alpha changed");
        const int canvasCreates=TestCanvasCreates;
        registration.methods.at("smoothingMode")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        fixture.native->clip=tTVPRect(0,0,4,3);
        call(fixture,"clear",{tTJSVariant(tjs_int64(0x40223344))});
        RequireDraw(TestCanvasCreates==canvasCreates && texture->pixels[0]==0 && texture->pixels[1]==0x40223344,
            "metadata call lost historical clip intersection");
        RequireDraw(texture->lastDirty==tTVPRect(0,0,4,3),"conservative dirty region narrowed");
        TestThrowFill=true;
        bool canvasThrew=false;
        try { call(fixture,"clear",{1}); } catch(const std::runtime_error&) { canvasThrew=true; }
        TestThrowFill=false;
        RequireDraw(canvasThrew,"canvas exception swallowed");
        RequireDraw(counts->unlocks==counts->writes,"exception left lease active");
        texture->AddRef();
        call(fixture,"clear",{9});
        RequireDraw(fixture.native->cow==1 && fixture.native->texture!=texture,"write COW missing");
        texture->Release();iTVPTexture2D::RecycleProcess();RequireDraw(counts->destroyed==1,"stable old texture retained by canvas");
        auto replacementCounts=fixture.native->texture->counters;
        fixture.native->Replace(new TestDrawTexture(2,5));
        iTVPTexture2D::RecycleProcess();
        RequireDraw(replacementCounts->destroyed==1,"stable replaced texture retained");
        registration.methods.at("record")->PropGet(0,nullptr,nullptr,&result,fixture.object);
        RequireDraw(fixture.native->texture->counters->writes==0,"metadata after resize acquired pixels");
        call(fixture,"clear",{13});
        RequireDraw(fixture.native->texture->pixels.size()==10 && fixture.native->texture->pixels.back()==13,"resize rebound wrong surface");
        TestDrawObject unknown(3,2,false);
        call(unknown,"clear",{5});auto oldUnknown=unknown.native->texture->counters;
        RequireDraw(unknown.native->texture->IsIndependent(),"software lifetime ref caused COW sharing");
        unknown.native->Replace(new TestDrawTexture(3,2,false));
        iTVPTexture2D::RecycleProcess();
        RequireDraw(oldUnknown->destroyed==0,"software cached surface lifetime lost");
        registration.methods.at("record")->PropGet(0,nullptr,nullptr,&result,unknown.object);
        call(unknown,"clear",{7});
        iTVPTexture2D::RecycleProcess();
        RequireDraw(oldUnknown->destroyed==1 && unknown.native->texture->pixels.back()==7,"software rebind did not release exact old texture");
        TestDrawObject replaced;
        auto original=replaced.native->texture->counters;
        {
            ncbInvocationPolicy<LayerExDraw>::Scope outer(TJS_N("clear"),ncbInvocationKind::Method,replaced.object);
            replaced.native->Replace(new TestDrawTexture(7,2));
            registration.methods.at("record")->PropGet(0,nullptr,nullptr,&result,replaced.object);
            replaced.Draw()->clear(11);
            RequireDraw(original->destroyed==0 && replaced.native->texture->counters->writes==0,
                "replacement changed nested operation's exact lease");
        }
        iTVPTexture2D::RecycleProcess();
        RequireDraw(original->unlocks==1 && original->dirty==1 && original->destroyed==1,
            "replacement unwind released/marked the wrong texture");
        call(replaced,"clear",{19});
        RequireDraw(replaced.native->texture->pixels.back()==19,"replacement after nested operation failed to rebind");
        TestDrawObject readOnly;
        auto readCounters=readOnly.native->texture->counters;
        {
            tTVPScopedLayerPixels read(readOnly.object,false,"test.readLifetime");
            readOnly.native->Replace(new TestDrawTexture(1,1));
            RequireDraw(read.Data()!=nullptr && read.Width()==4 && readCounters->destroyed==0,
                "read-only lease lost original replaced texture");
        }
        iTVPTexture2D::RecycleProcess();
        RequireDraw(readCounters->reads==1 && readCounters->dirty==0 && readCounters->unlocks==1 && readCounters->destroyed==1,
            "read-only lifetime acquired write/dirty or released wrong texture");
        TestDrawObject acquisitionFailure;
        acquisitionFailure.native->texture->fail=true;
        bool acquisitionThrew=false;
        try { call(acquisitionFailure,"clear",{1}); } catch(const std::runtime_error&) { acquisitionThrew=true; }
        RequireDraw(acquisitionThrew && acquisitionFailure.native->texture->IsIndependent() &&
            acquisitionFailure.native->texture->counters->dirty==0,"acquisition exception retained texture or marked dirty");
        acquisitionFailure.native->texture->fail=false;
        call(acquisitionFailure,"clear",{17});
        RequireDraw(acquisitionFailure.native->texture->counters->unlocks==1,"acquisition exception prevented later lease");
        RequireDraw(registration.methods.at("rawRecord")->PropSet(0,nullptr,nullptr,nullptr,fixture.object)==TJS_S_OK,
            "raw callback null-value behavior was replaced by normal-property validation");
        // MatrixConvertor is the production property-reading argument adapter.
        // NCBind gets the native instance before those user getters run.
        auto* matrixObject=MatrixObject();
        tTJSVariant matrixArgument(matrixObject,matrixObject);matrixObject->Release();
        TestDrawObject conversionFailure;
        registration.methods.at("record")->PropSet(0,nullptr,nullptr,&enabled,conversionFailure.object);
        auto conversionCounts=conversionFailure.native->texture->counters;
        TestMatrixGetter=[&] {
            RequireDraw(conversionCounts->writes==1,"argument getter ran before original instance acquisition order");
            throw std::runtime_error("matrix getter failure");
        };
        bool conversionThrew=false;
        try { call(conversionFailure,"setViewTransform",{matrixArgument}); }
        catch(const std::runtime_error&) { conversionThrew=true; }
        TestMatrixGetter={};
        RequireDraw(conversionThrew && conversionCounts->writes==1 && conversionCounts->unlocks==1,
            "throwing production argument conversion leaked/reordered writable lease");
        TestDrawObject reentry;
        registration.methods.at("record")->PropSet(0,nullptr,nullptr,&enabled,reentry.object);
        auto outerCounts=reentry.native->texture->counters;
        bool reentered=false;
        TestMatrixGetter=[&] {
            if(reentered) return;
            reentered=true;
            auto* originalTexture=reentry.Draw()->currentPixelAccess().Texture();
            RequireDraw(originalTexture==reentry.native->texture,"outer conversion lease missing");
            reentry.native->Replace(new TestDrawTexture(6,2));
            call(reentry,"clear",{25});
            RequireDraw(reentry.native->texture->pixels.back()==25 && reentry.Draw()->currentPixelAccess().Texture()==originalTexture,
                "external reentry redirected clear or failed to restore original lease");
        };
        call(reentry,"setViewTransform",{matrixArgument});TestMatrixGetter={};
        iTVPTexture2D::RecycleProcess();
        RequireDraw(outerCounts->writes==1 && outerCounts->unlocks==1 && outerCounts->destroyed==1 &&
            reentry.native->texture->counters->writes==1 && reentry.native->texture->counters->unlocks==1 &&
            reentry.native->texture->pixels.back()==25,"reentrant conversion did not preserve both exact targets");
        TestDrawObject reentryFailure;
        registration.methods.at("record")->PropSet(0,nullptr,nullptr,&enabled,reentryFailure.object);
        auto failedOuter=reentryFailure.native->texture->counters;
        reentered=false;
        TestMatrixGetter=[&] {
            if(reentered) return;reentered=true;
            auto* originalTexture=reentryFailure.Draw()->currentPixelAccess().Texture();
            reentryFailure.native->Replace(new TestDrawTexture(2,7));
            TestThrowFill=true;
            bool nestedThrew=false;
            try { call(reentryFailure,"clear",{29}); } catch(const std::runtime_error&) { nestedThrew=true; }
            TestThrowFill=false;
            RequireDraw(nestedThrew && reentryFailure.Draw()->currentPixelAccess().Texture()==originalTexture &&
                reentryFailure.native->texture->counters->unlocks==1,"reentrant exception lost outer state or new lease");
        };
        call(reentryFailure,"setViewTransform",{matrixArgument});TestMatrixGetter={};
        RequireDraw(failedOuter->writes==1 && failedOuter->unlocks==1,"reentrant exception leaked outer lease");
        call(reentryFailure,"clear",{31});
        RequireDraw(reentryFailure.native->texture->pixels.back()==31,"reentrant exception left stale canvas frame");
        TestDrawObject pureConversion;
        TestMatrixGetter=[&] {
            RequireDraw(pureConversion.native->texture->counters->writes==0,"pure transform acquired before argument getter");
            throw std::runtime_error("pure matrix getter failure");
        };
        conversionThrew=false;
        try { call(pureConversion,"setTransform",{matrixArgument}); } catch(const std::runtime_error&) { conversionThrew=true; }
        TestMatrixGetter={};
        RequireDraw(conversionThrew && pureConversion.native->texture->counters->writes==0,
            "pure throwing argument conversion acquired pixels");
        std::cout << "PASS C2A production LayerEx/NCBind boundaries (canvas double; native plutovg pending)\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "FAIL C2A LayerEx boundary: " << error.what() << '\n';return 1;
    }
}
