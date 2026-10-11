#include "backend/OrdinaryLayerPipeline.h"
#include <stdexcept>

void OrdinaryLayerPipelineTests() {
    using namespace krkrsdl3::ordinary_layer;
    const auto check=[](bool value){if(!value)throw std::runtime_error("ordinary Layer pipeline contract");};
    for(int opacity:{0,1,127,254,255}) {
        check(Select(TVP_LAYER_KIND_Copy,opacity,0)==Pipeline::Copy);
        check(Select(TVP_LAYER_KIND_Fill,opacity,0)==Pipeline::Fill);
        check(Select(TVP_LAYER_KIND_CopyColor,opacity,0)==Pipeline::CopyColor);
        check(Select(TVP_LAYER_KIND_Alpha,opacity,9)==(opacity==255?Pipeline::AlphaHDA:Pipeline::Generic));
    }
    for(uint32_t flags=0;flags<32;++flags) {
        check(Select(TVP_LAYER_KIND_Alpha,255,flags)==(flags==9?Pipeline::AlphaHDA:Pipeline::Generic));
        check(Select(TVP_LAYER_KIND_CopyColor,255,flags)==(flags==0?Pipeline::CopyColor:Pipeline::Generic));
    }
    for(unsigned kind=0;kind<TVP_LAYER_OPERATION_COUNT;++kind) {
        if(kind==TVP_LAYER_KIND_Copy || kind==TVP_LAYER_KIND_Fill || kind==TVP_LAYER_KIND_CopyColor)continue;
        check(Select(int(kind),255,0)==Pipeline::Generic);
    }
    check(WritesRGBOnly(Pipeline::AlphaHDA) && WritesRGBOnly(Pipeline::CopyColor));
    check(!WritesRGBOnly(Pipeline::Copy) && !WritesRGBOnly(Pipeline::Fill) && !WritesRGBOnly(Pipeline::Generic));
    // Logical alpha/COW contracts must survive removing the shader fetch.
    check(TVPLayerOperationReadsTarget(TVPLayerOperationKind::CopyColor));
    Counters counters;counters.enabled=true;counters.availableMask=31;
    counters.Record(Pipeline::Copy,12);counters.Record(Pipeline::Copy,20);
    counters.Record(Pipeline::AlphaHDA,8);
    check(counters.draws[1]==2 && counters.pixels[1]==32 && counters.draws[2]==0);
    check(counters.Report()=="metal.layerPipelines version=1 final=1 enabled=1 availableMask=31 genericDraws=0 genericPixels=0 copyDraws=2 copyPixels=32 fillDraws=0 fillPixels=0 copyColorDraws=0 copyColorPixels=0 alphaHDADraws=1 alphaHDAPixels=8");
}
