#include "tjsCommHead.h"
#include "gl/tvpgl.h"
#include "P1AShaderTests.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "ProductionUnivTransMath.inc"

extern "C" unsigned char TVPOpacityOnOpacityTable[65536];

uint32_t TVPTestP1APixel(uint32_t dst, uint32_t src, const TVPLayerOperation& op) {
    return op.kind == TVPLayerOperationKind::AdditiveAlphaToAlpha
        ? univ_shader::layerPremulToAlphaPixel(src)
        : univ_shader::layerP1APixel(dst,src,int(op.kind),op.opacity,op.flags);
}
uint32_t TVPTestP1AGammaPixel(uint32_t pixel,const TVPLayerOperation& op,const uint8_t* gamma768) {
    return univ_shader::layerGammaPixel(pixel,op.flags,gamma768);
}
uint32_t TVPTestP1AConstAlphaSDPixel(uint32_t input0,uint32_t input1,const TVPLayerOperation& op) {
    if(op.flags & TVP_LAYER_DEST_PREMULTIPLIED)
        return univ_shader::univTransBlendARGB(input0,input1,uint32_t(op.opacity));
    if(op.flags & TVP_LAYER_DEST_ALPHA)
        return univ_shader::constAlphaSDDestAlpha(input0,input1,uint32_t(op.opacity),TVPOpacityOnOpacityTable);
    return univ_shader::constAlphaSD(input0,input1,uint32_t(op.opacity));
}
namespace {
uint64_t exactPixels=0;
void Equal(uint32_t actual,uint32_t expected,const char* method,const TVPLayerOperation& op,
           uint32_t d,uint32_t s) {
    if(actual!=expected) {
        std::cerr<<"P1A scalar mismatch "<<method<<" kind="<<int(op.kind)<<" opacity="<<op.opacity
                 <<" flags="<<op.flags<<" dst="<<d<<" src="<<s
                 <<" expected="<<expected<<" actual="<<actual<<'\n';
        throw std::runtime_error("P1A production shader vs initialized TVP function pointer mismatch");
    }
    ++exactPixels;
}
uint32_t State=0xf3907251;
uint32_t Next() { State=State*1664525u+1013904223u; return State; }
void BlendTests() {
    using K=TVPLayerOperationKind;
    struct V {const char* name; K kind; uint32_t flags; decltype(TVPSubBlend) full; decltype(TVPSubBlend_o) opacity;};
    const V variants[]={
        {"SubBlend",K::Sub,TVP_LAYER_HOLD_ALPHA,TVPSubBlend_HDA,TVPSubBlend_HDA_o},
        {"MulBlend",K::Mul,0,TVPMulBlend,TVPMulBlend_o},
        {"MulBlend_HDA",K::Mul,TVP_LAYER_HOLD_ALPHA,TVPMulBlend_HDA,TVPMulBlend_HDA_o},
        {"ColorDodgeBlend",K::ColorDodge,TVP_LAYER_HOLD_ALPHA,TVPColorDodgeBlend_HDA,TVPColorDodgeBlend_HDA_o},
        {"DarkenBlend",K::Darken,TVP_LAYER_HOLD_ALPHA,TVPDarkenBlend_HDA,TVPDarkenBlend_HDA_o},
        {"LightenBlend",K::Lighten,TVP_LAYER_HOLD_ALPHA,TVPLightenBlend_HDA,TVPLightenBlend_HDA_o},
        {"ScreenBlend",K::Screen,TVP_LAYER_HOLD_ALPHA,TVPScreenBlend_HDA,TVPScreenBlend_HDA_o}};
    for(const auto& v:variants) {
        TVPLayerOperation op; op.kind=v.kind; op.flags=v.flags|TVP_LAYER_FULL_OPACITY_BRANCH;
        // Exhaust every pair of channel bytes at rounding/branch opacity edges.
        // Other channels use different permutations to expose packed-byte carry.
        for(int opa:{0,1,63,127,128,191,254,255}) for(unsigned dc=0;dc<256;++dc) {
            op.opacity=opa;
            std::array<uint32_t,256> d,s,expected;
            for(unsigned sc=0;sc<256;++sc) {
                d[sc]=dc|(((dc+67)&255)<<8)|(((dc*193)&255)<<16)|((Next()&255)<<24);
                s[sc]=sc|(((sc*137)&255)<<8)|(((sc+109)&255)<<16)|((Next()&255)<<24);
            }
            expected=d;
            if(opa==255) v.full(expected.data(),s.data(),256);
            else v.opacity(expected.data(),s.data(),256,opa);
            for(unsigned i=0;i<256;++i) Equal(TVPTestP1APixel(d[i],s[i],op),expected[i],v.name,op,d[i],s[i]);
        }
        // Every opacity, all source/destination alpha branch boundaries.
        for(int opa=0;opa<256;++opa) for(unsigned da:{0u,1u,127u,128u,254u,255u})
        for(unsigned sa:{0u,1u,127u,128u,254u,255u}) {
            op.opacity=opa;
            uint32_t d=(Next()&0xffffff)|(da<<24),s=(Next()&0xffffff)|(sa<<24),expected=d;
            if(opa==255) v.full(&expected,&s,1); else v.opacity(&expected,&s,1,opa);
            Equal(TVPTestP1APixel(d,s,op),expected,v.name,op,d,s);
        }
    }
}
void MaskAndConversionTests() {
    TVPLayerOperation op;op.kind=TVPLayerOperationKind::RemoveOpacity;
    // Exhaust the complete alpha x mask x opacity domain (16,777,216 pixels).
    for(int opa=0;opa<256;++opa) for(unsigned alpha=0;alpha<256;++alpha) {
        op.opacity=opa;
        std::array<uint32_t,256> d,expected;
        std::array<uint8_t,256> mask;
        for(unsigned m=0;m<256;++m) {mask[m]=uint8_t(m);d[m]=(Next()&0xffffff)|(alpha<<24);}
        expected=d;
        if(opa==255) TVPRemoveOpacity(expected.data(),mask.data(),256);
        else TVPRemoveOpacity_o(expected.data(),mask.data(),256,opa);
        for(unsigned m=0;m<256;++m) Equal(TVPTestP1APixel(d[m],m,op),expected[m],"RemoveOpacity",op,d[m],m);
    }
    op.kind=TVPLayerOperationKind::AdditiveAlphaToAlpha;
    for(unsigned alpha=0;alpha<256;++alpha) for(unsigned color=0;color<256;++color) {
        uint32_t s=color|(((color+79)&255)<<8)|(((color*149)&255)<<16)|(alpha<<24),expected=s;
        TVPConvertAdditiveAlphaToAlpha(&expected,1);
        Equal(TVPTestP1APixel(0,s,op),expected,"AdditiveAlphaToAlpha",op,0,s);
    }
}
void SDTests() {
    TVPLayerOperation op;op.kind=TVPLayerOperationKind::AlphaSD;
    for(int opa=0;opa<256;++opa) for(unsigned a:{0u,1u,127u,128u,254u,255u}) {
        op.opacity=opa;
        std::array<uint32_t,256> d,s,expected;
        for(unsigned c=0;c<256;++c) {d[c]=(Next()&0xffffff)|(a<<24);s[c]=c*0x010101u|((Next()&255)<<24);}
        TVPConstAlphaBlend_SD(expected.data(),d.data(),s.data(),256,opa);
        for(unsigned c=0;c<256;++c) Equal(TVPTestP1APixel(d[c],s[c],op),expected[c],"AlphaBlend_SD",op,d[c],s[c]);
        op.kind=TVPLayerOperationKind::ConstAlphaSD;op.flags=TVP_LAYER_DEST_PREMULTIPLIED;
        TVPConstAlphaBlend_SD_a(expected.data(),d.data(),s.data(),256,opa);
        for(unsigned c=0;c<256;++c) Equal(TVPTestP1AConstAlphaSDPixel(d[c],s[c],op),expected[c],"ConstAlphaBlend_SD_a",op,d[c],s[c]);
        op.kind=TVPLayerOperationKind::AlphaSD;op.flags=0;
    }
}
void GammaTests() {
    static_assert(sizeof(tTVPGLGammaAdjustTempData)==768,"Gamma LUT wire layout changed");
    static_assert(offsetof(tTVPGLGammaAdjustTempData,B)==0 && offsetof(tTVPGLGammaAdjustTempData,G)==256 &&
                  offsetof(tTVPGLGammaAdjustTempData,R)==512,"Gamma channel offsets changed");
    // Include normal, reversed, narrowed and different per-channel domains.
    const tTVPGLGammaAdjustData parameters[]={
        {1,0,255,1,0,255,1,0,255}, {0.5f,13,239,1.7f,29,211,2.5f,43,197},
        {2.2f,255,0,0.4f,203,17,1.3f,31,231}};
    for(const auto& parametersForLUT:parameters) {
        tTVPGLGammaAdjustTempData temp;
        TVPInitGammaAdjustTempData(&temp,&parametersForLUT);
        for(uint32_t flags:{0u,uint32_t(TVP_LAYER_DEST_PREMULTIPLIED)}) {
            TVPLayerOperation op;op.kind=TVPLayerOperationKind::AdjustGamma;op.flags=flags;
            for(unsigned alpha=0;alpha<256;++alpha) for(unsigned c=0;c<256;++c) {
                uint32_t pixel=c|(((c+37)&255)<<8)|(((c*173)&255)<<16)|(alpha<<24),expected=pixel;
                if(flags) TVPAdjustGamma_a(&expected,1,&temp);else TVPAdjustGamma(&expected,1,&temp);
                Equal(TVPTestP1AGammaPixel(pixel,op,reinterpret_cast<const uint8_t*>(&temp)),expected,
                      flags?"AdjustGamma_a":"AdjustGamma",op,pixel,0);
            }
            // Explicit historical LUT index overflow: recip*alpha>>8=256 for
            // alpha 4/8/16/32/64/128. Clamp255 must apply independently to RGB.
            for(unsigned alpha:{4u,8u,16u,32u,64u,128u}) for(unsigned channel=0;channel<3;++channel) {
                uint32_t pixel=(alpha<<24)|(alpha<<(channel*8)),expected=pixel;
                if(flags) TVPAdjustGamma_a(&expected,1,&temp);else TVPAdjustGamma(&expected,1,&temp);
                const auto* bytes=reinterpret_cast<const uint8_t*>(&temp);
                const uint32_t actual=TVPTestP1AGammaPixel(pixel,op,bytes);
                Equal(actual,expected,
                      "Gamma_a clamp255 channel regression",op,pixel,0);
                if(flags) {
                    const uint32_t endpoint=uint32_t(bytes[(2-channel)*256+255])*(alpha+(alpha>>7))>>8;
                    if(((actual>>(channel*8))&255u)!=endpoint)
                        throw std::runtime_error("Gamma_a overflow regression did not use channel LUT[255]");
                }
            }
        }
        TVPUninitGammaAdjustTempData(&temp);
    }
}
}
void P1AShaderTests() {
    BlendTests();MaskAndConversionTests();SDTests();GammaTests();
    std::cout<<"PASS P1A extracted production MSL vs initialized software bindings: "<<exactPixels<<" exact pixels\n";
}
