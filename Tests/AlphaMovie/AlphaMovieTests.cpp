#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>
#include "AMVDecodedFrameCache.h"
#include "LayerWorkDiagnostics.h"
using tjs_uint8=uint8_t; using tjs_uint16=uint16_t; using tjs_uint32=uint32_t;
using tjs_uint64=uint64_t; using tjs_int=int; using tjs_real=double; using tTJSString=std::string;
struct tTJSVariant {};
struct tTJSNativeInstance { virtual ~tTJSNativeInstance()=default; };
namespace TJS {
class tTJSBinaryStream {
    std::vector<uint8_t> bytes; size_t position=0;
public:
    explicit tTJSBinaryStream(std::vector<uint8_t> data):bytes(std::move(data)) {}
    size_t GetSize() const { return bytes.size(); }
    size_t GetPosition() const { return position; }
    void SetPosition(size_t value) { if(value>bytes.size()) throw std::runtime_error("invalid seek"); position=value; }
    void ReadBuffer(void* output,size_t size) {
        if(size>bytes.size()-position) throw std::runtime_error("truncated input");
        std::memcpy(output,bytes.data()+position,size); position+=size;
    }
};
}
using TJS::tTJSBinaryStream;
std::vector<uint8_t> fixture;
tTJSBinaryStream* TVPCreateStream(const tTJSString&) { return new tTJSBinaryStream(fixture); }
void EnsureAlphaMovieTablesInitialized() {}
struct tTVPRect { tTVPRect(int,int,int,int) {} };
struct tTVPBaseTexture {
    int width,height;
    tTVPBaseTexture(int w,int h,int):width(w),height(h) {}
    int GetWidth() const { return width; } int GetHeight() const { return height; }
    void Fill(const tTVPRect&,int) {}
};
struct BufferManager { uint8_t* data; size_t size; BufferManager(uint8_t* d,size_t n):data(d),size(n) {} };
size_t decodes=0;
// Pixel decoding is a deterministic boundary double. The index, seek, zlib,
// lazy decode/cache and cleanup below are the exact production method bodies.
void DecodeAndConvertToRGBA(BufferManager* input,uint8_t[3][64],uint8_t* output,
    unsigned width,unsigned height,uint8_t* alpha,bool) {
    if(!input->size) throw std::runtime_error("missing color data");
    ++decodes;
    for(size_t i=0;i<size_t(width)*height;++i) {
        output[i*4]=input->data[0]; output[i*4+1]=0x21; output[i*4+2]=0x43;
        output[i*4+3]=alpha ? alpha[i] : 255;
    }
}
#include "ProductionAlphaMovie.inc"
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void Integer(std::vector<uint8_t>& out,uint32_t value,int bytes=4) { for(int i=0;i<bytes;++i) out.push_back(uint8_t(value>>(8*i))); }
std::vector<uint8_t> Movie(unsigned count,unsigned width=2,unsigned height=2,bool zipped=false) {
    std::vector<uint8_t> out{'A','J','P','M'};
    Integer(out,0); Integer(out,1); Integer(out,40+(zipped ? 128 : 192)); Integer(out,0);
    Integer(out,count); Integer(out,0); Integer(out,30); Integer(out,width,2); Integer(out,height,2); Integer(out,zipped ? 2 : 1);
    out.resize(40+(zipped ? 128 : 192),0);
    for(unsigned i=0;i<count;++i) {
        std::vector<uint8_t> alpha,encoded;
        if(zipped) {
            alpha.assign(size_t(width)*height,uint8_t(i+20)); uLongf size=compressBound(alpha.size()); encoded.resize(size);
            Require(compress2(encoded.data(),&size,alpha.data(),alpha.size(),Z_BEST_SPEED)==Z_OK,"fixture compression failed"); encoded.resize(size);
        }
        out.insert(out.end(),{'F','R','A','M'}); Integer(out,unsigned(encoded.size()+1)+(zipped ? 16 : 12));
        Integer(out,i); Integer(out,width,2); Integer(out,height,2); Integer(out,width,2); Integer(out,height,2);
        if(zipped) Integer(out,encoded.size());
        out.insert(out.end(),encoded.begin(),encoded.end()); out.push_back(uint8_t(i+1));
    }
    return out;
}
int main() {
    try {
        for(bool zipped:{false,true}) {
            fixture=Movie(135,2,2,zipped); decodes=0;
            tTJSNI_AlphaMovie movie; movie.open("fixture.amv");
            Require(movie.frameInfoList.size()==135 && decodes==0 && movie.decodedFrames.Bytes()==0,"open eagerly decoded frames");
            Require(movie.DecodeFrame(100)[0]==101 && movie.DecodeFrame(0)[0]==1,"seek decoded wrong frame");
            Require(movie.DecodeFrame(0)[3]==(zipped ? 20 : 255),"zlib alpha or color routing changed");
            Require(decodes==2,"cached seek decoded twice");
            for(size_t i=1;i<35;++i) movie.DecodeFrame(i);
            Require(movie.decodedFrames.Count()<=AMVDecodedFrameCache::MaxEntries,"tiny frames exceeded entry bound");
            movie.clear(); Require(!movie.filePtr && !movie.m_BmpBits && movie.decodedFrames.Count()==0,"clear retained old movie state");
            movie.open("fixture.amv"); Require(movie.DecodeFrame(0)[0]==1,"reopen borrowed stale decoded data");
        }
        fixture=Movie(12,1024,1024); tTJSNI_AlphaMovie large; large.open("large.amv");
        for(size_t i=0;i<12;++i) large.DecodeFrame(i);
        Require(large.decodedFrames.Bytes()<=AMVDecodedFrameCache::Budget,"large movie exceeded decoded budget");
        const auto before=decodes; large.DecodeFrame(0); Require(decodes==before+1,"evicted frame was not re-decoded");
        fixture=Movie(1); fixture.pop_back(); tTJSNI_AlphaMovie invalid;
        bool rejected=false; try { invalid.open("invalid.amv"); } catch(const std::runtime_error&) { rejected=true; }
        Require(rejected,"truncated indexed payload accepted");
        Require(!invalid.filePtr && invalid.frameInfoList.empty() && !invalid.m_BmpBits,"failed open retained partial movie");
        std::cout<<"PASS production AMV lazy indexing/decode, seek, alpha, bounded LRU, reload and malformed payload\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
