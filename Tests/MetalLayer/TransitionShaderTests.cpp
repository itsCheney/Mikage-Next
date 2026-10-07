#include "LayerTransitionGeometry.h"
#include <algorithm>
#include <cstring>
#include <array>
#include <iostream>
#include <stdexcept>
#include "ProductionUnivTransMath.inc"
#include "ProductionTransitionMath.inc"

extern "C" unsigned char TVPOpacityOnOpacityTable[65536];
uint32_t TVPTestTransitionPixel(const TVPLayerTransitionOperation& op,int x,int y,
        const uint8_t* s1,int pitch1,const uint8_t* s2,int pitch2) {
    const int dummyRows[26]{}; const uint16_t dummyTable[8]{};
    return transition_shader::transitionPixel(x,y,op.params,
        op.rows ? reinterpret_cast<const int*>(op.rows->data()) : dummyRows,
        op.table ? reinterpret_cast<const uint16_t*>(op.table->data()) : dummyTable,
        TVPOpacityOnOpacityTable,{s1,pitch1},{s2,pitch2});
}
void TransitionContractTests() {
    auto require=[](bool ok,const char* message) {if(!ok) throw std::runtime_error(message);};
    static_assert(int(TVPLayerTransitionKind::Mosaic)==1 && int(TVPLayerTransitionKind::RotateSwap)==7 &&
                  int(TVPLayerTransitionKind::Count)==8,"transition IDs");
    TVPLayerTransitionOperation op; auto& p=op.params;
    p.frameWidth=16;p.frameHeight=16;p.width=16;p.height=16;p.blockSize=2;
    for(int kind:{-1,0,8,9,2147483647}) {
        p.kind=kind;
        require(layer_transition::Validate(op,16,16,16,16,16,16)==TVPLayerTransitionResult::Unsupported,
                "invalid transition kind accepted");
    }
    p.kind=1;
    require(layer_transition::Validate(op,16,16,16,16,16,16)==TVPLayerTransitionResult::Applied,"mosaic preflight rejected");
    p.destLeft=1;
    require(layer_transition::Validate(op,16,16,16,16,16,16)==TVPLayerTransitionResult::InvalidGeometry,"target overflow accepted");
    p.destLeft=0;p.blockSize=0;
    require(layer_transition::Validate(op,16,16,16,16,16,16)==TVPLayerTransitionResult::InvalidParameters,"zero block accepted");
    p.kind=2;p.flags=0;
    require(layer_transition::Validate(op,16,16,16,16,16,16)==TVPLayerTransitionResult::InvalidParameters,"missing wave rows accepted");
    std::array<int32_t,16> shifts{};op.rows=TVPMakeTransitionBytes(shifts.data(),sizeof(shifts));
    require(layer_transition::Validate(op,16,16,16,16,16,16)==TVPLayerTransitionResult::Applied,"wave rows rejected");
    TVPLayerTransitionOperation turn;
    turn.params.kind=4;turn.params.frameWidth=128;turn.params.frameHeight=64;turn.params.width=64;turn.params.height=64;
    turn.params.left=64;turn.params.offsetX=2;turn.params.phase=INT32_MIN;
    std::vector<uint8_t> table((64*64*8+64)*4);
    turn.table=TVPMakeTransitionBytes(table.data(),table.size());
    require(layer_transition::Validate(turn,64,64,128,64,128,64)==TVPLayerTransitionResult::InvalidParameters,
        "turn phase signed overflow accepted");
    uint32_t state=19;uint64_t pixels=0;
    const auto next=[&](){state=state*1664525u+1013904223u;return state;};
    for(int ratio=0;ratio<256;++ratio) for(int i=0;i<128;++i) {
        uint32_t a=next(),b=next(),expected=0;
        for(unsigned shift=0;shift<32;shift+=8) {
            uint32_t v=(a>>shift)&255;
            expected|=((v+((((b>>shift)&255)-v)*uint32_t(ratio)>>8))&255)<<shift;
        }
        require(transition_shader::transitionBlend(a,b,ratio)==expected,"extrans byte blend mismatch");++pixels;
    }
    std::cout<<"PASS transition contract and production blend: "<<pixels<<" exact pixels\n";
}
