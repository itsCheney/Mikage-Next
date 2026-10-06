#include "LayerRenderOperation.h"
#include "LayerOperationShaderDefinitions.h"
#include "backend/MetalLayerShaders.h"
#include "backend/MetalRenderBackend.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if(!condition) throw std::runtime_error(message);
}

// The fixture expands the same enum/traits/MSL rows as production, then
// appends one kind. Its Count never changes the production enum or registry.
enum class ExtendedKind : uint32_t {
#define TVP_LAYER_OPERATION TVP_LAYER_OPERATION_ENUM_ROW
#define TVP_LAYER_OPERATION_COUNT(count)
#include "LayerOperationDefinitions.def"
#undef TVP_LAYER_OPERATION_COUNT
#define TVP_LAYER_OPERATION_COUNT(count) Count = count
#include "OperationContractExtension.def"
#undef TVP_LAYER_OPERATION_COUNT
#undef TVP_LAYER_OPERATION
};
constexpr TVPLayerOperationTraits extendedTraits[] = {
#define TVP_LAYER_OPERATION TVP_LAYER_OPERATION_TRAIT_ROW
#define TVP_LAYER_OPERATION_COUNT(count)
#include "LayerOperationDefinitions.def"
#include "OperationContractExtension.def"
#undef TVP_LAYER_OPERATION_COUNT
#undef TVP_LAYER_OPERATION
};
constexpr char extendedMSL[] =
#define TVP_LAYER_OPERATION TVP_LAYER_MSL_CONSTANT
#define TVP_LAYER_OPERATION_COUNT(count)
#include "LayerOperationDefinitions.def"
#include "OperationContractExtension.def"
#undef TVP_LAYER_OPERATION
    "bool layerNeedsSource(int kind) { switch(kind) {\n"
#define TVP_LAYER_OPERATION TVP_LAYER_MSL_NEEDS_SOURCE
#include "LayerOperationDefinitions.def"
#include "OperationContractExtension.def"
#undef TVP_LAYER_OPERATION
    "default: return false; } }\n"
    "bool layerReadsTarget(int kind) { switch(kind) {\n"
#define TVP_LAYER_OPERATION TVP_LAYER_MSL_READS_TARGET
#include "LayerOperationDefinitions.def"
#include "OperationContractExtension.def"
#undef TVP_LAYER_OPERATION
#undef TVP_LAYER_OPERATION_COUNT
    "default: return false; } }\n";
static_assert(TVPLayerOperationDefinitionsValid(extendedTraits), "extension must use stable contiguous IDs");
static_assert(sizeof(extendedTraits)/sizeof(extendedTraits[0]) == static_cast<uint32_t>(ExtendedKind::Count),
              "extension traits follow extension Count");

void StableIDsAndTraits() {
    // Golden ABI/diagnostic list, independent of the production .def expansion.
    const char* expected[] = {"Unsupported", "Copy", "CopyColor", "CopyMask", "CopyOpaque", "Fill",
        "FillColor", "FillMask", "Alpha", "ConstAlpha", "ColorMap", "FillBlend", "RemoveConstOpacity",
        "ConstAlphaSD", "UnivTrans", "AdditiveAlpha", "PsMul", "PsOverlay", "PsHardLight",
        "AlphaToAdditiveAlpha", "GrayScale", "CopyBlueToAlpha", "MultiplyAlpha", "BoxBlur",
        "PsScreen", "PsColorDodge5", "Add", "Sub", "Mul", "ColorDodge", "Darken", "Lighten", "Screen", "RemoveOpacity", "AdditiveAlphaToAlpha", "AdjustGamma", "AlphaSD", "PsAlpha", "PsAdd", "PsSub", "PsSoftLight", "PsColorDodge", "PsColorBurn", "PsLighten", "PsDarken", "PsDiff", "PsDiff5", "PsExclusion"};
    Require(TVP_LAYER_OPERATION_COUNT == sizeof(expected)/sizeof(expected[0]), "production stable Count changed");
    const std::string msl=TVP_LAYER_OPERATION_MSL_DEFINITIONS;
    for(uint32_t id=0;id<TVP_LAYER_OPERATION_COUNT;++id) {
        const auto& traits=TVP_LAYER_OPERATION_TRAITS[id];
        Require(static_cast<uint32_t>(traits.kind)==id && !std::strcmp(traits.name,expected[id]),
                "stable operation ID/name changed");
        Require(msl.find(std::string("constant int TVP_LAYER_KIND_")+expected[id]+" = "+std::to_string(id)+";")!=std::string::npos,
                "MSL operation ID disagrees with C++");
        const auto* found=TVPGetLayerOperationTraits(static_cast<TVPLayerOperationKind>(id));
        Require(id ? found==&traits : found==nullptr, "unsupported or valid trait lookup changed");
        if(id) {
            Require(traits.backendInputCount<=3 && traits.targetFormat==TVPLayerTextureFormat::RGBA8,
                    "operation input/target format contract invalid");
            const std::string sourceRow="case "+std::to_string(id)+": return "+std::to_string(traits.backendInputCount)+" != 0;";
            Require(msl.find(sourceRow)!=std::string::npos, "MSL source dependency disagrees with traits");
            Require(TVPLayerOperationNeedsSource(traits.kind)==(traits.backendInputCount!=0),
                    "host source helper disagrees with traits");
            const std::string targetRow="case "+std::to_string(id)+": return "+(traits.readsTarget?"true":"false")+";";
            Require(msl.find(targetRow)!=std::string::npos, "MSL destination dependency disagrees with traits");
        }
    }
    for(auto id : {uint32_t(TVP_LAYER_OPERATION_COUNT), uint32_t(TVP_LAYER_OPERATION_COUNT+1),
                    std::numeric_limits<uint32_t>::max()})
        Require(!TVPGetLayerOperationTraits(static_cast<TVPLayerOperationKind>(id)), "invalid kind has traits");
    Require(TVPGetLayerOperationTraits(TVPLayerOperationKind::ColorMap)->sourceFormats[0]==TVPLayerTextureFormat::R8,
            "ColorMap lost mask source format");
    Require(TVPGetLayerOperationTraits(TVPLayerOperationKind::ConstAlphaSD)->backendInputCount==2 &&
            TVPGetLayerOperationTraits(TVPLayerOperationKind::UnivTrans)->backendInputCount==3 &&
            TVPGetLayerOperationTraits(TVPLayerOperationKind::UnivTrans)->sourceFormats[2]==TVPLayerTextureFormat::R8,
            "dual/triple source contract changed");
    for(auto kind : {TVPLayerOperationKind::AlphaToAdditiveAlpha,TVPLayerOperationKind::GrayScale,
                    TVPLayerOperationKind::AdditiveAlphaToAlpha}) {
        const auto* traits=TVPGetLayerOperationTraits(kind);
        Require(traits->logicalInputCountMask==3 && traits->backendInputCount==1 &&
                traits->referenceRule==TVPLayerReferenceRule::UsedWhenNoInput,
                "reference-based conversion contract changed");
    }
    Require(TVPGetLayerOperationTraits(TVPLayerOperationKind::Copy)->geometries==
            (TVP_LAYER_GEOMETRY_RECT|TVP_LAYER_GEOMETRY_AFFINE_COPY_SUBSET), "Copy affine subset lost");
    Require(TVPGetLayerOperationTraits(TVPLayerOperationKind::RemoveOpacity)->sourceFormats[0]==TVPLayerTextureFormat::R8,
            "RemoveOpacity lost mask format");
    const auto* gamma=TVPGetLayerOperationTraits(TVPLayerOperationKind::AdjustGamma);
    Require(gamma->logicalInputCountMask==1 && gamma->backendInputCount==0 && gamma->readsTarget &&
            gamma->referenceRule==TVPLayerReferenceRule::Ignored &&
            gamma->parameterResources==TVP_LAYER_RESOURCE_GAMMA_LUT,
            "Gamma target-reading owned LUT contract changed");
    for(auto kind : {TVPLayerOperationKind::PsSoftLight,TVPLayerOperationKind::PsColorDodge,
                    TVPLayerOperationKind::PsColorBurn})
        Require(TVPGetLayerOperationTraits(kind)->parameterResources==TVP_LAYER_RESOURCE_PS_TABLES,
                "PS table operation lost immutable initialized table dependency");
    for(uint32_t id=2;id<TVP_LAYER_OPERATION_COUNT;++id)
        Require(TVP_LAYER_OPERATION_TRAITS[id].geometries==TVP_LAYER_GEOMETRY_RECT,
                "P0 enabled new geometry");
}

void AlphaAndAliasExceptions() {
    using K=TVPLayerOperationKind;
    TVPLayerOperation op;
    const K hdaKinds[]={K::Alpha,K::ConstAlpha,K::ColorMap,K::FillBlend,K::AdditiveAlpha,
        K::PsMul,K::PsOverlay,K::PsHardLight,K::PsScreen,K::PsColorDodge5,K::Add,K::Sub,K::ColorDodge,K::Darken,K::Lighten,K::Screen,K::PsAlpha,K::PsAdd,K::PsSub,K::PsSoftLight,K::PsColorDodge,K::PsColorBurn,K::PsLighten,K::PsDarken,K::PsDiff,K::PsDiff5,K::PsExclusion};
    for(auto kind:hdaKinds) for(uint32_t flags=0;flags<16;++flags) {
        op.kind=kind; op.flags=flags;
        const bool expected=(flags&TVP_LAYER_HOLD_ALPHA) &&
            !(flags&(TVP_LAYER_DEST_ALPHA|TVP_LAYER_DEST_PREMULTIPLIED));
        Require(TVPLayerOperationPreservesAlpha(op,false)==expected, "HDA/d/a alpha rule changed");
    }
    for(auto kind:{K::CopyColor,K::FillColor,K::Mul,K::AdjustGamma}) {
        op.kind=kind; Require(TVPLayerOperationPreservesAlpha(op,false), "color-only operation writes alpha");
    }
    for(auto kind:{K::AlphaToAdditiveAlpha,K::GrayScale,K::AdditiveAlphaToAlpha}) {
        op.kind=kind;
        Require(TVPLayerOperationPreservesAlpha(op,true) && !TVPLayerOperationPreservesAlpha(op,false),
                "COW/reference conversion incorrectly preserves target alpha");
    }
    for(auto kind:{K::Copy,K::CopyMask,K::CopyOpaque,K::Fill,K::FillMask,K::RemoveConstOpacity,
                  K::ConstAlphaSD,K::UnivTrans,K::CopyBlueToAlpha,K::MultiplyAlpha,K::BoxBlur,K::RemoveOpacity,K::AlphaSD}) {
        op.kind=kind; Require(!TVPLayerOperationPreservesAlpha(op,true), "alpha-writing operation marked preserving");
    }
    for(auto kind:{K::AdditiveAlpha,K::PsMul,K::PsOverlay,K::PsHardLight,K::PsScreen,K::PsColorDodge5,K::Add,K::MultiplyAlpha,K::ConstAlphaSD,
                  K::Sub,K::Mul,K::ColorDodge,K::Darken,K::Lighten,K::Screen,K::AlphaSD,K::PsAlpha,K::PsAdd,K::PsSub,K::PsSoftLight,K::PsColorDodge,K::PsColorBurn,K::PsLighten,K::PsDarken,K::PsDiff,K::PsDiff5,K::PsExclusion})
        Require(TVPGetLayerOperationTraits(kind)->aliasRule==TVPLayerAliasRule::SamePixelOnly,
                "sequential offset alias contract changed");
    Require(TVPGetLayerOperationTraits(K::UnivTrans)->aliasRule==TVPLayerAliasRule::Snapshot &&
            TVPGetLayerOperationTraits(K::BoxBlur)->aliasRule==TVPLayerAliasRule::ReadAllBeforeWrite,
            "transition/blur snapshot distinction lost");
    op.kind=K::Alpha; op.flags=TVP_LAYER_DEST_ALPHA;
    Require(TVPLayerOperationNeedsAlphaTables(op), "destination alpha table dependency lost");
    op.flags=TVP_LAYER_DEST_PREMULTIPLIED;
    Require(!TVPLayerOperationNeedsAlphaTables(op), "premultiplied formula incorrectly needs table");
    op.kind=K::Add; op.flags=TVP_LAYER_DEST_ALPHA;
    Require(!TVPLayerOperationNeedsAlphaTables(op), "integer Add formula incorrectly needs table");
}

void ShaderCompositionAndExtension() {
    const std::string prefix=TVP_LAYER_OPERATION_MSL_DEFINITIONS;
    for(const char* options : {"", "#define TVP_LAYER_IN_PLACE 1\n", "#define TVP_LAYER_FRAMEBUFFER_FETCH 1\n"}) {
        const auto source=TVPBuildMetalLayerShaderSource(options);
        Require(source.find(std::string(options)+prefix)==0, "runtime Metal variant omitted shared definitions");
        Require(source.find("int4 layerPixel(")!=std::string::npos, "runtime Metal variant omitted pixel implementation");
        Require(source.find("const device uchar* gamma [[buffer(2)]]")!=std::string::npos &&
                source.find("layerGammaPixel(layerPack(d),uint(flags),gamma)")!=std::string::npos &&
                source.find("layerPixel(d,s,color,kind,opa,flags,tables,gamma,psTables)")!=std::string::npos &&
                source.find("layerPixel(d,s,p.color,kind,p.operation.y,p.operation.z,tables,gamma,psTables)")!=std::string::npos,
                "Gamma LUT is not shared by compute and tile pixel paths");
        Require(source.find("const device uchar* psTables [[buffer(3)]]")!=std::string::npos &&
                source.find("layerPsP1BPixel(layerPack(d),layerPack(s),kind,opa,uint(flags),psTables)")!=std::string::npos,
                "PS immutable table buffer/helper omitted from shared pixel paths");
        Require(source.find("if (layerNeedsSource(kind))")!=std::string::npos &&
                source.find("if(layerNeedsSource(kind))")!=std::string::npos &&
                source.find("bool overwrite = !layerReadsTarget(kind);")!=std::string::npos,
                "runtime Metal dependency helpers are not consumed by both paths");
        Require(source.find("TVP_LAYER_KIND_TestKind")==std::string::npos, "fixture kind leaked into production shader");
    }
    const uint32_t added=static_cast<uint32_t>(ExtendedKind::TestKind);
    Require(added==TVP_LAYER_OPERATION_COUNT && static_cast<uint32_t>(ExtendedKind::Count)==added+1,
            "extension did not append a new kind");
    const auto* traits=TVPFindLayerOperationTraits(extendedTraits,added);
    Require(traits && traits->backendInputCount==1 && traits->sourceFormats[0]==TVPLayerTextureFormat::R8,
            "appended kind lost traits/source dependency");
    Require(!TVPFindLayerOperationTraits(extendedTraits,static_cast<uint32_t>(ExtendedKind::Count)),
            "extension Count is treated as an operation");
    const std::string fixtureSource=extendedMSL;
    Require(fixtureSource.find("constant int TVP_LAYER_KIND_TestKind = 48;")!=std::string::npos &&
            fixtureSource.find("case 48: return 1 != 0;")!=std::string::npos &&
            fixtureSource.find("case 48: return true;")!=std::string::npos,
            "appended kind lost shader ID/dependency definitions");
    krkrsdl3::metal_diagnostics::LayerKindPixelCounters<static_cast<uint32_t>(ExtendedKind::Count)> counts;
    counts.RecordKindPixels(int(ExtendedKind::Copy),7);
    counts.RecordKindPixels(int(ExtendedKind::TestKind),13);
    counts.RecordKindPixels(int(ExtendedKind::Count),100);
    counts.RecordKindPixels(-1,100);
    counts.RecordKindPixels(std::numeric_limits<int>::max(),100);
    Require(counts.pixelsByKind.back()==13 && counts.KindPixelSummary()=="1:7,48:13",
            "Count extension lost diagnostic tail or formatter/bounds");
    krkrsdl3::metal_diagnostics::Workload workload;
    workload.Rect(int(TVPLayerOperationKind::PsExclusion),19,false,false,false);
    workload.Rect(int(TVPLayerOperationKind::BoxBlur),23,false,false,false);
    workload.RecordKindPixels(int(TVPLayerOperationKind::Count),100);
    workload.RecordKindPixels(-1,100);
    Require(workload.pixelsByKind.size()==TVP_LAYER_OPERATION_COUNT && workload.pixelsByKind.back()==19 &&
            workload.blurPixels==23 && workload.KindPixelSummary()=="23:23,47:19",
            "production diagnostic Count/blur/tail contract changed");
}
}

void OperationContractTests() {
    StableIDsAndTraits();
    AlphaAndAliasExceptions();
    ShaderCompositionAndExtension();
    std::cout<<"PASS Layer operation contract: stable IDs, traits, shared MSL variants, test-only Count extension\n";
}
