#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using ttstr=std::string;using tjs_int=int;using tjs_uint64=uint64_t;using tjs_uint8=uint8_t;
using tTVPGraphicMetaInfoPair=std::pair<std::string,std::string>;
bool TVPAllocGraphicCacheOnHeap=false,failTexture=false;
namespace TVPTextureFormat {enum e {Gray=1,RGB=3,RGBA=4};}
struct tTVPBitmap {
    int refs=1,w,h,bpp;std::vector<uint8_t> pixels;
    tTVPBitmap(int width,int height,int bits):w(width),h(height),bpp(bits),pixels(size_t(w)*h*(bpp/8)){}
    int GetWidth(){return w;}int GetHeight(){return h;}int GetBPP(){return bpp;}
    void AddRef(){++refs;}void Release(){if(!--refs)delete this;}
    void* GetScanLine(int y){return pixels.data()+size_t(y)*w*(bpp/8);}
};
struct iTVPTexture2D {
    static int live;int refs=1,w,h;TVPTextureFormat::e format;std::vector<uint8_t> pixels;
    iTVPTexture2D(int width,int height,TVPTextureFormat::e f):w(width),h(height),format(f){++live;}
    ~iTVPTexture2D(){--live;}void AddRef(){++refs;}void Release(){if(!--refs)delete this;}
    int GetWidth(){return w;}int GetHeight(){return h;}TVPTextureFormat::e GetFormat(){return format;}
};
int iTVPTexture2D::live=0;
struct tTVPBaseBitmap {
    std::vector<uint8_t> pixels;int w=0,h=0,bpp=0;
    void AssignBitmap(tTVPBitmap* bmp){pixels=bmp->pixels;w=bmp->w;h=bmp->h;bpp=bmp->bpp;}
    void Recreate(int width,int height,int bits){w=width;h=height;bpp=bits;pixels.resize(size_t(w)*h*(bpp/8));}
    void* GetScanLineForWrite(int y){return pixels.data()+size_t(y)*w*(bpp/8);}
};
struct iTVPBaseBitmap {std::vector<uint8_t> pixels;void AssignTexture(iTVPTexture2D* tex){pixels=tex->pixels;}};
struct Manager {
    iTVPTexture2D* CreateTexture2D(const void* data,int pitch,int w,int h,TVPTextureFormat::e f) {
        if(failTexture)throw std::bad_alloc();
        auto* tex=new iTVPTexture2D(w,h,f);tex->pixels.resize(size_t(w)*h*int(f));
        for(int y=0;y<h;++y)std::memcpy(tex->pixels.data()+size_t(y)*w*int(f),static_cast<const uint8_t*>(data)+size_t(y)*pitch,w*int(f));
        return tex;
    }
    iTVPTexture2D* CreateTexture2D(tTVPBitmap* bmp){return CreateTexture2D(bmp->pixels.data(),bmp->w*(bmp->bpp/8),bmp->w,bmp->h,TVPTextureFormat::e(bmp->bpp/8));}
} manager;
Manager* TVPGetRenderManager(){return &manager;}
#include "ProductionImageHolder.inc"
}
void ImageHolderTests() {
    const auto check=[](bool v){if(!v)throw std::runtime_error("production cache holder");};
    for(bool heap:{false,true})for(int bpp:{8,32}) {
        TVPAllocGraphicCacheOnHeap=heap;tTVPGraphicImageData holder;
        auto* bitmap=new tTVPBitmap(4,3,bpp);
        for(size_t i=0;i<bitmap->pixels.size();++i)bitmap->pixels[i]=uint8_t(i);
        const auto expected=bitmap->pixels;holder.AssignBitmap(bitmap);bitmap->Release();
        check(holder.HasBitmap() && holder.GetSize()==expected.size());
        TVPAllocGraphicCacheOnHeap=!heap;tTVPBaseBitmap dest;holder.AssignToBitmap(&dest);
        check(dest.pixels==expected); // representation belongs to the entry
        failTexture=true;iTVPBaseBitmap gpu;
        try {holder.AssignToTexture(&gpu);check(false);}catch(const std::bad_alloc&){}
        check(holder.HasBitmap() && gpu.pixels.empty());
        failTexture=false;holder.AssignToTexture(&gpu);
        check(gpu.pixels==expected && !holder.HasBitmap());
        holder.AssignToTexture(&gpu);check(gpu.pixels==expected);
    }
    check(iTVPTexture2D::live==0);TVPAllocGraphicCacheOnHeap=false;
}
