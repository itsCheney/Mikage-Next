#include "ImagePrefetchPolicy.h"
#include "LayerHotspotContext.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using tjs_uint=unsigned;using tjs_int=int;
struct ttstr:std::string {using std::string::string;ttstr(const std::string& s):std::string(s){}
    ttstr()=default;bool IsEmpty()const{return empty();}void Clear(){clear();}};
#define TJS_N(s) s
constexpr int glmNormal=0,TVP_clNone=-1,TJS_S_TRUE=1,TVP_EPT_IMMEDIATE=1;
enum tTVPGraphicPixelFormat {gpfLuminance,gpfRGB,gpfPalette,gpfRGBA};
struct tTVPBitmap {
    static int live;static bool destroyedReferenced;int refs=1,w,h;bool IsOpaque=false;std::vector<uint8_t> bytes;
    tTVPBitmap(int width,int height,int):w(width),h(height),bytes(size_t(w)*h*4){++live;}
    ~tTVPBitmap(){if(refs>1)destroyedReferenced=true;--live;}void AddRef(){++refs;}
    void Release(){if(!--refs)delete this;}int GetWidth(){return w;}int GetHeight(){return h;}
    int GetPitch(){return w*4;}void* GetScanLine(int y){return bytes.data()+y*w*4;}
};
int tTVPBitmap::live=0;
bool tTVPBitmap::destroyedReferenced=false;
using tTVPGraphicMetaInfoPair=std::pair<ttstr,ttstr>;
struct iTJSDispatch2 {int id=0,refs=1;void AddRef(){++refs;}void Release(){--refs;}
    int IsValid(int,void*,void*,iTJSDispatch2*){return TJS_S_TRUE;}};
struct tTJSVariant {int number=0;tTJSVariant()=default;tTJSVariant(iTJSDispatch2*,iTJSDispatch2*){}
    tTJSVariant& operator=(int n){number=n;return *this;}tTJSVariant& operator=(const ttstr&){return *this;}
    tTJSVariant& operator=(const char*){return *this;}};
struct tTJSNI_Bitmap {bool loading=true;int tags=0;std::vector<uint8_t> bytes;
    uint64_t GetReceiverDiagnosticID(){return 42;}void* GetBitmap(){return this;}
    void SetLoading(bool value){loading=value;}void SetSizeAndImageBuffer(tTVPBitmap* bmp){bytes=bmp->bytes;}};
void TVPTagGraphicDiagnosticAsset(void* bitmap,const ttstr&){++static_cast<tTJSNI_Bitmap*>(bitmap)->tags;}
struct tTVPTmpBitmapImage {tTVPBitmap* bmp=nullptr;std::vector<tTVPGraphicMetaInfoPair>* MetaInfo=nullptr;
    krkrsdl3::image_prefetch::Policy<ttstr>* prefetchPolicy=nullptr;krkrsdl3::image_prefetch::Ticket prefetch;
    ~tTVPTmpBitmapImage(){delete bmp;delete MetaInfo;}};
struct tTVPImageLoadCommand {iTJSDispatch2* owner_=nullptr;tTJSNI_Bitmap* bmp_=nullptr;
    ttstr path_,result_;tTVPTmpBitmapImage* dest_=nullptr;bool failed_=false;krkrsdl3::image_prefetch::Ticket prefetch_;
    ~tTVPImageLoadCommand(){if(owner_)owner_->Release();delete dest_;}};
struct tTJSCriticalSection{};struct tTJSCriticalSectionHolder {explicit tTJSCriticalSectionHolder(tTJSCriticalSection&){} };
struct Event {int waits=0;void Set(){}void WaitFor(int){++waits;}};
struct tTVPAsyncImageLoader {
    tTJSCriticalSection CommandQueueCS,ImageQueueCS;Event PushCommandQueueEvent;
    std::queue<tTVPImageLoadCommand*> CommandQueue,LoadedQueue;krkrsdl3::image_prefetch::Policy<ttstr> PrefetchPolicy;
    bool GetTerminated(){return PushCommandQueueEvent.waits>1;}void SendToLoadFinish(){}
    void HandleLoadedImage();void PushLoadQueue(iTJSDispatch2*,tTJSNI_Bitmap*,const ttstr&);
    bool PushPrefetch(const ttstr&,const std::shared_ptr<krkrsdl3::image_prefetch::Budget>&);
    void LoadingThread();void LoadImageFromCommand(tTVPImageLoadCommand*);
};
uint64_t nowMS=1;uint64_t TVPGetTickCount(){return nowMS;}
std::map<ttstr,std::vector<uint8_t>> cache;
std::vector<tTVPBitmap*> cacheHeld;
std::vector<int> callbacks;std::vector<bool> errors;
bool TVPHasImageCache(const ttstr& key,int,int,int,int){return cache.count(key)!=0;}
void TVPPushGraphicCache(const ttstr& key,tTVPBitmap* bmp,std::vector<tTVPGraphicMetaInfoPair>* meta){
    cache[key]=bmp->bytes;bmp->AddRef();cacheHeld.push_back(bmp);delete meta;
}
iTJSDispatch2 dictionary;iTJSDispatch2* TVPMetaInfoPairsToDictionary(std::vector<tTVPGraphicMetaInfoPair>*){return &dictionary;}
void TVPPostEvent(iTJSDispatch2* owner,iTJSDispatch2*,const ttstr&,int,int,int,tTJSVariant* params){
    if(params[1].number!=1)throw std::runtime_error("ordinary async callback mode changed");
    callbacks.push_back(owner->id);errors.push_back(params[2].number!=0);
}
void TVPAddImportantLog(const ttstr&){}
const char *TVPUnknownGraphicFormat="unknown",*TVPImageLoadError="decode";
ttstr TVPFormatMessage(const char* type,const ttstr& path){return std::string(type)+path;}
struct tTVPStreamHolder {explicit tTVPStreamHolder(const ttstr&){}void* Get(){return nullptr;}};
std::function<void()> duringDecode;
struct tTVPRegisterGraphicInfo {
    void* FormatData=nullptr;
    void Load(void*,void* dest,int(*size)(void*,unsigned,unsigned,tTVPGraphicPixelFormat),
        void*(*line)(void*,int),void(*meta)(void*,const ttstr&,const ttstr&),void*,int key,int mode) {
        if(key!=-1 || mode!=glmNormal)throw std::runtime_error("prefetch decode parameters");
        if(duringDecode)duringDecode();
        size(dest,4,4,gpfRGBA);for(int y=0;y<4;++y)std::memset(line(dest,y),0x65,16);
        meta(dest,"format","normal");
    }
} handler;
tTVPRegisterGraphicInfo* TVPGetGraphicLoadHandler(const ttstr& path){return path=="bad.png"?nullptr:&handler;}
#include "ProductionImageAsync.inc"
}
void ImageAsyncTests() {
    const auto check=[](bool v){if(!v)throw std::runtime_error("production async image path");};
    cache.clear();callbacks.clear();errors.clear();nowMS=1;
    tTVPAsyncImageLoader loader;auto budget=std::make_shared<krkrsdl3::image_prefetch::Budget>(256,100);
    check(loader.PushPrefetch("a.png",budget));check(!loader.PushPrefetch("a.png",budget));
    duringDecode=[&]{check(!loader.PushPrefetch("a.png",budget));};loader.LoadingThread();duringDecode={};
    check(loader.LoadedQueue.size()==1 && callbacks.empty());
    check(!loader.PushPrefetch("a.png",budget)); // loaded still participates in dedupe
    cache["a.png"]=std::vector<uint8_t>(64,0x99);loader.HandleLoadedImage();
    check(cache["a.png"][0]==0x99 && callbacks.empty()); // demand won the race
    loader.PushCommandQueueEvent.waits=0;
    check(loader.PushPrefetch("b.png",budget));krkrsdl3::image_prefetch::Cancel();
    loader.LoadingThread();loader.HandleLoadedImage();check(!cache.count("b.png"));
    loader.PushCommandQueueEvent.waits=0;
    check(loader.PushPrefetch("c.png",budget));loader.LoadingThread();nowMS=100;
    loader.HandleLoadedImage();check(!cache.count("c.png"));
    nowMS=1;loader.PushCommandQueueEvent.waits=0;
    auto small=std::make_shared<krkrsdl3::image_prefetch::Budget>(63,100);
    check(loader.PushPrefetch("large.png",small));loader.LoadingThread();loader.HandleLoadedImage();
    check(!cache.count("large.png"));
    loader.PushCommandQueueEvent.waits=0;
    check(loader.PushPrefetch("good.png",budget));loader.LoadingThread();loader.HandleLoadedImage();
    check(cache["good.png"]==std::vector<uint8_t>(64,0x65) && callbacks.empty());
    check(!tTVPBitmap::destroyedReferenced && tTVPBitmap::live==1);
    // Ordinary requests sharing a key retain every owner and callback in order.
    loader.PushCommandQueueEvent.waits=0;iTJSDispatch2 a,b,c;a.id=1;b.id=2;c.id=3;
    tTJSNI_Bitmap ba,bb,bc;
    loader.PushLoadQueue(&a,&ba,"same.png");loader.PushLoadQueue(&b,&bb,"same.png");loader.PushLoadQueue(&c,&bc,"bad.png");
    loader.LoadingThread();loader.HandleLoadedImage();
    check(callbacks==std::vector<int>({1,2,3}) && errors==std::vector<bool>({false,false,true}));
    check(ba.bytes==bb.bytes && ba.bytes==std::vector<uint8_t>(64,0x65));
    check(!ba.loading && !bb.loading && !bc.loading && a.refs==1 && b.refs==1 && c.refs==1);
    check(ba.tags==1 && bb.tags==1 && bc.tags==0);
    check(!tTVPBitmap::destroyedReferenced);
    for(auto* held:cacheHeld)held->Release();cacheHeld.clear();
    check(tTVPBitmap::live==0 && loader.CommandQueue.empty() && loader.LoadedQueue.empty());
}
