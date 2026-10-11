#include "tjsCommHead.h"
#include "gl/tvpgl.h"
#include "P1BShaderTests.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include "ProductionUnivTransMath.inc"

uint32_t TVPTestP1BPixel(uint32_t d,uint32_t s,const TVPLayerOperation& op,const uint8_t* psTables) {
    return univ_shader::layerPsP1BPixel(d,s,int(op.kind),op.opacity,op.flags,psTables);
}
namespace {
using K=TVPLayerOperationKind;
uint64_t pixels=0;
uint32_t randomState=0xb1465a7c;
uint32_t Next() {randomState=randomState*1664525u+1013904223u;return randomState;}
void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void Equal(uint32_t actual,uint32_t expected,const char* name,const TVPLayerOperation& op,uint32_t d,uint32_t s) {
    if(actual!=expected) {
        std::cerr<<"P1B scalar mismatch "<<name<<" kind="<<int(op.kind)<<" opacity="<<op.opacity
                 <<" dst="<<d<<" src="<<s<<" expected="<<expected<<" actual="<<actual<<'\n';
        throw std::runtime_error("P1B production MSL vs initialized software binding mismatch");
    }
    ++pixels;
}
struct Variant {
    const char* name; K kind;
    decltype(TVPPsAlphaBlend) full;
    decltype(TVPPsAlphaBlend_o) opacity;
};


void TableLayoutAndEndpoints(const uint8_t* tables) {
    Require(TVPGetPsBlendTable(3)==nullptr && TVPGetPsBlendTable(0)!=nullptr &&
            TVPGetPsBlendTable(1)!=nullptr && TVPGetPsBlendTable(2)!=nullptr,
            "PS table read-only export contract changed");
    for(unsigned sc=0;sc<256;++sc) for(unsigned dc=0;dc<256;++dc) {
        const unsigned dodge=(255-sc)<=dc ? 255 : dc*255/(255-sc);
        const unsigned burn=sc<=(255-dc) ? 0 : 255-(255-dc)*255/sc;
        Require(tables[65536+sc*256+dc]==dodge,"ColorDodge table layout/formula differs");
        Require(tables[131072+sc*256+dc]==burn,"ColorBurn table layout/formula differs");
    }
    for(unsigned sc=0;sc<256;++sc) {
        Require(tables[sc*256]==0 && tables[sc*256+255]==255,"SoftLight table black/white endpoints differ");
        Require(tables[65536+255*256+sc]==255,"ColorDodge source255 endpoint differs");
        Require(tables[131072+sc]==0,"ColorBurn source0 endpoint differs");
    }
}
void CompleteChannelPairs(const uint8_t* tables) {
    const Variant variants[]={
        {"PsAlphaBlend",K::PsAlpha,TVPPsAlphaBlend_HDA,TVPPsAlphaBlend_HDA_o},
        {"PsAddBlend",K::PsAdd,TVPPsAddBlend_HDA,TVPPsAddBlend_HDA_o},
        {"PsSubBlend",K::PsSub,TVPPsSubBlend_HDA,TVPPsSubBlend_HDA_o},
        {"PsSoftLightBlend",K::PsSoftLight,TVPPsSoftLightBlend_HDA,TVPPsSoftLightBlend_HDA_o},
        {"PsColorDodgeBlend",K::PsColorDodge,TVPPsColorDodgeBlend_HDA,TVPPsColorDodgeBlend_HDA_o},
        {"PsColorBurnBlend",K::PsColorBurn,TVPPsColorBurnBlend_HDA,TVPPsColorBurnBlend_HDA_o},
        {"PsLightenBlend",K::PsLighten,TVPPsLightenBlend_HDA,TVPPsLightenBlend_HDA_o},
        {"PsDarkenBlend",K::PsDarken,TVPPsDarkenBlend_HDA,TVPPsDarkenBlend_HDA_o},
        {"PsDiffBlend",K::PsDiff,TVPPsDiffBlend_HDA,TVPPsDiffBlend_HDA_o},
        {"PsDiff5Blend",K::PsDiff5,TVPPsDiff5Blend_HDA,TVPPsDiff5Blend_HDA_o},
        {"PsExclusionBlend",K::PsExclusion,TVPPsExclusionBlend_HDA,TVPPsExclusionBlend_HDA_o}};
    for(const auto& v:variants) {
        TVPLayerOperation op;op.kind=v.kind;op.flags=TVP_LAYER_HOLD_ALPHA|TVP_LAYER_FULL_OPACITY_BRANCH;
        // All 65,536 channel pairs, independent source/target alpha boundaries,
        // and opacity edges. Different channel permutations expose packing errors.
        for(int opacity:{0,1,63,127,128,191,254,255})
        for(unsigned alpha:{0u,1u,127u,128u,254u,255u}) for(unsigned dc=0;dc<256;++dc) {
            op.opacity=opacity;
            std::array<uint32_t,256> d,s,expected;
            for(unsigned sc=0;sc<256;++sc) {
                d[sc]=dc|(((dc*137)&255)<<8)|(((dc+73)&255)<<16)|((Next()&255)<<24);
                s[sc]=sc|(((sc+109)&255)<<8)|(((sc*193)&255)<<16)|(alpha<<24);
            }
            expected=d;
            if(opacity==255) v.full(expected.data(),s.data(),256);
            else v.opacity(expected.data(),s.data(),256,opacity);
            for(unsigned sc=0;sc<256;++sc) Equal(TVPTestP1BPixel(d[sc],s[sc],op,tables),expected[sc],v.name,op,d[sc],s[sc]);
        }
        // Every source alpha x every opacity, with alpha/carry/color edges in RGB.
        for(int opacity=0;opacity<256;++opacity) for(unsigned alpha=0;alpha<256;++alpha) {
            op.opacity=opacity;
            std::array<uint32_t,16> d,s,expected;
            constexpr uint32_t edge[]={0,1,63,127,128,191,254,255};
            for(unsigned i=0;i<d.size();++i) {
                d[i]=(Next()&0xffffff)|(edge[i%8]<<24);
                s[i]=(Next()&0xffffff)|(alpha<<24);
                if(i<8) {d[i]=edge[i]*0x010101u|(edge[7-i]<<24);s[i]=edge[7-i]*0x010101u|(alpha<<24);}
            }
            expected=d;
            if(opacity==255) v.full(expected.data(),s.data(),int(s.size()));
            else v.opacity(expected.data(),s.data(),int(s.size()),opacity);
            for(unsigned i=0;i<d.size();++i) Equal(TVPTestP1BPixel(d[i],s[i],op,tables),expected[i],v.name,op,d[i],s[i]);
        }
        // Independent HDA and zero-strength invariants, not implementation copies.
        for(int opacity:{0,1,127,128,254,255}) {
            op.opacity=opacity;
            for(unsigned i=0;i<512;++i) {
                uint32_t d=Next(),s=Next(),actual=TVPTestP1BPixel(d,s,op,tables);
                Require((actual&0xff000000u)==(d&0xff000000u),"P1B HDA writes destination alpha");
                if(opacity==0) Require(actual==d,"P1B zero opacity did not preserve destination");
                s&=0xffffffu;
                Require(TVPTestP1BPixel(d,s,op,tables)==d,"P1B source alpha0 did not preserve destination");
            }
        }
    }
}
void DistinctLegacyVariants(const uint8_t* tables) {
    TVPLayerOperation op;op.flags=TVP_LAYER_HOLD_ALPHA|TVP_LAYER_FULL_OPACITY_BRANCH;op.opacity=255;
    const uint32_t d=0x43505050u,s=0x80c8c8c8u;
    op.kind=K::PsColorDodge;
    const auto dodge=TVPTestP1BPixel(d,s,op,tables);
    const auto dodge5=univ_shader::layerPsPixel(d,s,int(K::PsColorDodge5),255,op.flags);
    Require(dodge==0x43a7a7a7u && dodge5==0x43838383u,"Dodge/Dodge5 independent counterexample changed");
    uint32_t expected=d;TVPPsColorDodgeBlend_HDA(&expected,&s,1);
    Equal(dodge,expected,"Dodge counterexample binding",op,d,s);
    expected=d;TVPPsColorDodge5Blend_HDA(&expected,&s,1);
    Equal(dodge5,expected,"Dodge5 unchanged binding",op,d,s);
    const uint32_t diffDst=0x635a5a5au;
    op.kind=K::PsDiff;
    const auto diff=TVPTestP1BPixel(diffDst,s,op,tables);
    op.kind=K::PsDiff5;
    const auto diff5=TVPTestP1BPixel(diffDst,s,op,tables);
    Require(diff==0x63646464u && diff5==0x630a0a0au,"Diff/Diff5 independent counterexample changed");
    // Ps full-opacity is still alpha/256 interpolation, not an endpoint copy.
    op.kind=K::PsAlpha;
    Require(TVPTestP1BPixel(0x7e000000u,0xffffffffu,op,tables)==0x7efefefeu,
            "PsAlpha full source alpha must retain /256 rounding");
}
}
void P1BShaderTests() {
    // Take the pointers after TVPInitTVPGL. Never regenerate SoftLight with a
    // separate pow() implementation or replace software's initialized bytes.
    std::array<uint8_t,196608> tables;
    for(unsigned i=0;i<3;++i) {
        const auto* table=TVPGetPsBlendTable(i);
        Require(table!=nullptr,"Initialized PS table export missing");
        std::copy_n(table,65536,tables.data()+i*65536);
    }
    TableLayoutAndEndpoints(tables.data());CompleteChannelPairs(tables.data());DistinctLegacyVariants(tables.data());
    std::cout<<"PASS P1B production MSL vs initialized PS HDA bindings: "<<pixels<<" exact pixels\n";
}
