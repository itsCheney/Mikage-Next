#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
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
TJS::tTJSBinaryStream* TVPCreateStream(const tTJSString&) { return new TJS::tTJSBinaryStream(fixture); }
struct tTVPRect { tTVPRect(int,int,int,int) {} };
struct tTVPBaseTexture {
    int width,height;
    tTVPBaseTexture(int w,int h,int):width(w),height(h) {}
    int GetWidth() const { return width; } int GetHeight() const { return height; }
    void Fill(const tTVPRect&,int) {}
};
// Stream and canvas allocation are doubles. Huffman, DCT, RGBA conversion,
// index, zlib alpha and lazy cache below are the production implementation.
#include "ProductionAMVCodec.inc"
#include "ProductionAlphaMovie.inc"
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void Integer(std::vector<uint8_t>& out,uint32_t value,int bytes=4) {
    for(int i=0;i<bytes;++i) out.push_back(uint8_t(value>>(8*i)));
}
struct Bits {
    std::vector<uint8_t> bytes; unsigned count=0;
    void Put(uint32_t value,unsigned length) {
        for(unsigned i=length;i>0;--i) {
            if(count%8==0) bytes.push_back(0);
            bytes.back()|=((value>>(i-1))&1)<<(7-count%8); ++count;
        }
    }
    void Symbol(const _CompactHuffmanSpec& spec,unsigned symbol) {
        uint32_t lengths[256]{},codes[256]{};
        auto n=BuildHuffmanCodeLengths(spec,lengths); BuildCanonicalHuffmanCodes(spec,n,codes);
        for(size_t i=0;i<n;++i) if(spec.values[i]==symbol) { Put(codes[i],lengths[i]); return; }
        throw std::runtime_error("fixture symbol missing");
    }
    void DC(bool chroma,int difference) {
        unsigned n=0; for(unsigned value=unsigned(std::abs(difference));value;value>>=1) ++n;
        Symbol(chroma ? kJpegChromaDcSpec : kJpegLumaDcSpec,n);
        if(n) Put(difference>=0 ? difference : ((1<<n)-1)+difference,n);
        Symbol(chroma ? kJpegChromaAcSpec : kJpegLumaAcSpec,0); // EOB
    }
};
std::vector<uint8_t> Movie(bool zipped) {
    std::vector<uint8_t> out{'A','J','P','M'};
    Integer(out,0); Integer(out,1); Integer(out,40+(zipped ? 128 : 192)); Integer(out,0);
    Integer(out,135); Integer(out,0); Integer(out,30); Integer(out,16,2); Integer(out,16,2); Integer(out,zipped ? 2 : 1);
    out.resize(40+(zipped ? 128 : 192),1);
    const int values[]={0,1,127,128,254,255};
    for(unsigned i=0;i<135;++i) {
        Bits bits; bits.DC(true,0); bits.DC(true,0);
        int predictor=0;
        for(int block=0;block<(zipped ? 4 : 8);++block) {
            int dc=(values[(i+(block<4 ? 0 : 3))%6]-128)*8;
            bits.DC(false,dc-predictor); predictor=dc;
        }
        bits.bytes.resize(std::max<size_t>(32,bits.bytes.size()+8),0);
        std::vector<uint8_t> alpha,encoded;
        if(zipped) {
            alpha.resize(256); for(size_t p=0;p<alpha.size();++p) alpha[p]=uint8_t(p+i);
            uLongf size=compressBound(alpha.size()); encoded.resize(size);
            Require(compress2(encoded.data(),&size,alpha.data(),alpha.size(),Z_BEST_SPEED)==Z_OK,"compress failed"); encoded.resize(size);
        }
        out.insert(out.end(),{'F','R','A','M'}); Integer(out,unsigned(encoded.size()+bits.bytes.size())+(zipped ? 16 : 12));
        Integer(out,i); Integer(out,16,2); Integer(out,16,2); Integer(out,16,2); Integer(out,16,2);
        if(zipped) Integer(out,encoded.size());
        out.insert(out.end(),encoded.begin(),encoded.end()); out.insert(out.end(),bits.bytes.begin(),bits.bytes.end());
    }
    return out;
}
int main() {
    try {
        const int values[]={0,1,127,128,254,255};
        for(bool zipped:{false,true}) {
            fixture=Movie(zipped); tTJSNI_AlphaMovie movie;
            krkrsdl3::layer_work::SetEnabled(true); movie.open("codec.amv");
            Require(krkrsdl3::layer_work::Take().decodedFrames==0,"open decoded pixels");
            for(size_t i=0;i<135;++i) {
                const auto& rgba=movie.DecodeFrame(i);
                for(size_t p=0;p<256;++p) {
                    Require(rgba[p*4]==values[i%6] && rgba[p*4+1]==values[i%6] && rgba[p*4+2]==values[i%6],"real AMV color differs");
                    Require(rgba[p*4+3]==(zipped ? uint8_t(p+i) : values[(i+3)%6]),"real AMV alpha differs");
                }
                auto sample=krkrsdl3::layer_work::Take(); Require(sample.decodedFrames==1,"frame decode attribution missing");
                movie.DecodeFrame(i); Require(krkrsdl3::layer_work::Take().decodedFrames==0,"cache hit decoded again");
            }
            movie.DecodeFrame(0); Require(krkrsdl3::layer_work::Take().decodedFrames==1,"loop did not decode evicted frame");
            Require(movie.DecodeFrame(100)[0]==values[100%6],"random seek wrong after loop");
            krkrsdl3::layer_work::SetEnabled(false);
        }
        std::cout<<"PASS real AMV Huffman/DCT/RGBA, both alpha formats, 135-frame playback, cached seek and loop\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
