#include "tjsCommHead.h"
#include "gl/tvpgl.h"
#include "LayerRenderOperation.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include "ProductionUnivTransMath.inc"

extern "C" unsigned char TVPOpacityOnOpacityTable[65536];
extern "C" unsigned char TVPNegativeMulTable[65536];

// The device double uses the SAME integer functions as the MSL kernel. This
// checks shader math on Linux; texture bindings/Metal compilation still need CI.
uint32_t TVPTestUnivTransPixel(uint32_t s1,uint32_t s2,uint8_t rule,const TVPLayerOperation& op) {
    static const auto tables=[]() {
        std::array<unsigned char,131072> result;
        std::copy_n(TVPOpacityOnOpacityTable,65536,result.begin());
        std::copy_n(TVPNegativeMulTable,65536,result.begin()+65536);
        return result;
    }();
    return univ_shader::univTransPixel(s1,s2,rule,op.phase,op.vague,op.flags,tables.data());
}

void UnivTransShaderTests() {
    struct Variant {
        uint32_t flags;
        decltype(TVPUnivTransBlend) blend;
        decltype(TVPUnivTransBlend_switch) switched;
    };
    Variant variants[]={{0,TVPUnivTransBlend,TVPUnivTransBlend_switch},
        {TVP_LAYER_DEST_ALPHA,TVPUnivTransBlend_d,TVPUnivTransBlend_switch_d},
        {TVP_LAYER_DEST_PREMULTIPLIED,TVPUnivTransBlend_a,TVPUnivTransBlend_switch_a}};
    uint32_t random=0x715abc39;
    const auto next=[&random]() { random=random*1664525u+1013904223u; return random; };
    const unsigned alphas[]={0,1,63,127,128,191,254,255};
    uint64_t pixels=0;
    for(const auto& variant:variants) for(int vague:{0,1,16,64,255,511,512,1024})
    for(int phase:{-1,0,1,63,127,255,256,255+vague}) {
        TVPLayerOperation op; op.kind=TVPLayerOperationKind::UnivTrans;
        op.flags=variant.flags; op.phase=phase; op.vague=vague;
        std::array<uint32_t,256> table,s1,s2,expected;
        std::array<uint8_t,256> rule;
        TVPInitUnivTransBlendTable(table.data(),phase,vague);
        for(unsigned a1:alphas) for(unsigned a2:alphas) {
            for(unsigned i=0;i<256;++i) {
                rule[i]=uint8_t(i); // every rule byte, including both threshold boundaries
                s1[i]=(next()&0xffffffu)|(a1<<24);
                s2[i]=(next()&0xffffffu)|(a2<<24);
                if(i%4==0) s1[i]=a1<<24; // black, white, equal and random colors
                if(i%4==1) s2[i]=(a2<<24)|0xffffffu;
                if(i%4==2) s2[i]=s1[i];
            }
            if(vague<512) variant.switched(expected.data(),s1.data(),s2.data(),rule.data(),table.data(),256,phase,phase-vague);
            else variant.blend(expected.data(),s1.data(),s2.data(),rule.data(),table.data(),256);
            for(unsigned i=0;i<256;++i) {
                const auto actual=TVPTestUnivTransPixel(s1[i],s2[i],rule[i],op);
                if(actual!=expected[i]) {
                    std::cerr<<"UnivTrans MSL math mismatch flags="<<op.flags<<" phase="<<phase
                             <<" vague="<<vague<<" rule="<<i<<" a1="<<a1<<" a2="<<a2
                             <<" expected="<<expected[i]<<" actual="<<actual<<'\n';
                    throw std::runtime_error("UnivTrans shader integer parity failed");
                }
                ++pixels;
            }
        }
    }
    std::cout<<"PASS production UnivTrans MSL integer helpers vs tvpgl: "<<pixels<<" exact pixels\n";
}
