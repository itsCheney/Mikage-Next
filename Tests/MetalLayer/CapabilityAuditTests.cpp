#include "tjsCommHead.h"
#include "RenderManager.h"
#include "MetalLayerRenderManager.h"
#include "CapabilityAuditTests.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

extern unsigned TVPTestMessageBoxes;

namespace {
struct Capability {
    const char* name;
    const char* canonical;
    bool registered;
    TVPLayerOperationKind kind;
    uint32_t flags;
};
constexpr Capability capabilities[] = {
#define CAPABILITY(name, canonical, registered, kind, flags) \
    {name, canonical, registered, TVPLayerOperationKind::kind, flags},
#include "LayerCapabilities.def"
#undef CAPABILITY
};
static_assert(sizeof(capabilities) / sizeof(capabilities[0]) == 85,
              "Appendix A must contain all 85 names");

struct SoftwareContract {
    const char* name;
    unsigned inputs;
    const char* formats;
    const char* parameters;
    const char* reference;
    const char* alpha;
    const char* binding;
    const char* semantics;
};
constexpr SoftwareContract softwareContracts[] = {
#define GAP(name, inputs, formats, parameters, reference, alpha, binding, semantics) \
    {name, inputs, formats, parameters, reference, alpha, binding, semantics},
#include "SoftwareCapabilityContracts.def"
#undef GAP
};
static_assert(sizeof(softwareContracts) / sizeof(softwareContracts[0]) == 24,
              "Keep all 24 original software contracts across mapping additions");

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Capability audit: " + message);
}
const SoftwareContract* Software(const char* name) {
    for (const auto& c : softwareContracts)
        if (!std::strcmp(name, c.name)) return &c;
    return nullptr;
}
std::string Quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << char(c);
    }
    out << '"';
    return out.str();
}
const char* Bool(bool value) { return value ? "true" : "false"; }
const char* Format(TVPLayerTextureFormat value) {
    return value == TVPLayerTextureFormat::R8 ? "R8" : "RGBA8";
}
const char* Alias(TVPLayerAliasRule value) {
    switch (value) {
        case TVPLayerAliasRule::Ignored: return "ignored";
        case TVPLayerAliasRule::Snapshot: return "snapshot subject to route checks";
        case TVPLayerAliasRule::SamePixelOnly: return "same-pixel only; shifted overlap falls back";
        case TVPLayerAliasRule::ReadAllBeforeWrite: return "read all input before write";
        default: return "unsupported";
    }
}
const char* Alpha(TVPLayerAlphaRule value) {
    switch (value) {
        case TVPLayerAlphaRule::Preserves: return "preserve target alpha";
        case TVPLayerAlphaRule::PlainHDA: return "preserve only HOLD_ALPHA without DEST_ALPHA/PREMULTIPLIED";
        case TVPLayerAlphaRule::SourceAlpha: return "copy source alpha; preserve target alpha only for same target source";
        default: return "writes alpha";
    }
}
const char* ParameterSpec(const Capability& c) {
    if (const auto* software = Software(c.name)) return software->parameters;
    using K = TVPLayerOperationKind;
    switch (c.kind) {
        case K::Fill: case K::FillColor: return "color:0";
        case K::FillBlend: return "color:0,opacity:1";
        case K::ColorMap: return "opacity:0,color:1";
        case K::FillMask: case K::RemoveConstOpacity: case K::Alpha:
        case K::ConstAlpha: case K::ConstAlphaSD: case K::AdditiveAlpha:
        case K::PsMul: case K::PsOverlay: case K::PsHardLight:
        case K::PsScreen: case K::PsColorDodge5: case K::Add: return "opacity:0";
        case K::UnivTrans: return "phase:0,vague:1";
        case K::BoxBlur: return "area_left:0,area_top:1,area_right:2,area_bottom:3";
        default: return "";
    }
}
struct Parameter { std::string name; int id; };
std::vector<Parameter> Parameters(const Capability& c) {
    std::vector<Parameter> result;
    std::istringstream input(ParameterSpec(c));
    std::string token;
    while (std::getline(input, token, ',')) {
        const auto colon = token.find(':');
        Require(colon != std::string::npos, "invalid parameter fixture");
        result.push_back({token.substr(0, colon), std::stoi(token.substr(colon + 1))});
    }
    return result;
}
void WriteParameters(std::ostream& out, const Capability& c) {
    out << '[';
    bool comma = false;
    for (const auto& p : Parameters(c)) {
        if (comma) out << ',';
        comma = true;
        const bool color = p.name == "color", opacity = p.name == "opacity";
        const bool gamma = p.name == "gammaAdjustData";
        out << "{\"name\":" << Quote(p.name) << ",\"id\":" << p.id
            << ",\"setter\":" << Quote(color ? "SetParameterColor4B" : opacity ? "SetParameterOpa" : gamma ? "SetParameterPtr" : "SetParameterInt")
            << ",\"domain\":" << Quote(color ? "uint32 ARGB" : opacity ? "0..255 baseline" : gamma ? "tTVPGLGammaAdjustData; values copied to owned LUT by setter" : c.kind == TVPLayerOperationKind::BoxBlur ? "inclusive signed area edges; positive kernel dimensions; GPU sums <=64 MiB" : "transition phase/vague; existing runtime domain checks apply")
            << '}';
    }
    out << ']';
}
void WriteGpuContract(std::ostream& out, const TVPLayerOperation& operation) {
    const auto* traits = TVPGetLayerOperationTraits(operation.kind);
    if (!traits) { out << "null"; return; }
    out << "{\"logicalInputCounts\":[";
    bool comma = false;
    for (unsigned i = 0; i <= 3; ++i) if (traits->logicalInputCountMask & (1u << i)) {
        if (comma) out << ',';
        comma = true;
        out << i;
    }
    out << "],\"backendInputCount\":" << unsigned(traits->backendInputCount)
        << ",\"targetFormat\":" << Quote(Format(traits->targetFormat))
        << ",\"sourceFormats\":[";
    for (unsigned i = 0; i < traits->backendInputCount; ++i) {
        if (i) out << ',';
        out << Quote(Format(traits->sourceFormats[i]));
    }
    out << "],\"reference\":" << Quote(traits->referenceRule == TVPLayerReferenceRule::UsedWhenNoInput ? "used for zero-input call" : "ignored")
        << ",\"readsTarget\":" << Bool(traits->readsTarget)
        << ",\"forwardSourceRequired\":" << Bool(TVPLayerOperationRequiresForwardSource(operation.kind))
        << ",\"alpha\":" << Quote(Alpha(traits->alphaRule))
        << ",\"preservesAlphaForTargetSource\":" << Bool(TVPLayerOperationPreservesAlpha(operation, true))
        << ",\"preservesAlphaForDistinctSource\":" << Bool(TVPLayerOperationPreservesAlpha(operation, false))
        << ",\"alias\":" << Quote(Alias(traits->aliasRule))
        << ",\"parameterResources\":" << traits->parameterResources
        << ",\"requiresPsBlendTables\":" << Bool(traits->parameterResources & TVP_LAYER_RESOURCE_PS_TABLES)
        << ",\"psBlendTableLayout\":" << ((traits->parameterResources & TVP_LAYER_RESOURCE_PS_TABLES)
            ? Quote("196608 bytes; SoftLight@0, ColorDodge@65536, ColorBurn@131072; each source*256+destination") : "null")
        << ",\"requiresAlphaTablesForFlags\":" << Bool(TVPLayerOperationNeedsAlphaTables(operation))
        << ",\"geometry\":{\"rect\":" << Bool(traits->geometries & TVP_LAYER_GEOMETRY_RECT)
        << ",\"affineCopySubset\":" << Bool((traits->geometries & TVP_LAYER_GEOMETRY_AFFINE_COPY_SUBSET) && operation.flags == 0)
        << ",\"affineBlendSubset\":" << Bool((traits->geometries & TVP_LAYER_GEOMETRY_AFFINE_BLEND_SUBSET) && TVPLayerOperationSupportsAffine(operation))
        << ",\"perspective\":false,\"arbitraryTriangles\":false}"
        << ",\"limitations\":" << Quote("Descriptor is semantic metadata, not an execution guarantee. Existing format, extent, sampling, opacity, source-range, alias, CPU residency and backend checks still apply; affine is Copy/flags=0 or declared blend families, one RGBA source, count=2, integer forward ROI, stretch 0..2 and accepted quad/clip only. Blend rectangle aliases require the same pixel rectangle; nonrectangular aliases use a source snapshot; triangle reference is ignored.") << '}';
}
void WriteSoftwareContract(std::ostream& out, const SoftwareContract* c) {
    if (!c) { out << "null"; return; }
    out << "{\"baselineInputCount\":" << c->inputs
        << ",\"formats\":" << Quote(c->formats)
        << ",\"reference\":" << Quote(c->reference)
        << ",\"alpha\":" << Quote(c->alpha)
        << ",\"binding\":" << Quote(c->binding)
        << ",\"semantics\":" << Quote(c->semantics)
        << ",\"geometry\":" << Quote("verified minimum: positive equal-size in-bounds rectangles; no arbitrary triangles or perspective contract inferred")
        << ",\"alias\":" << Quote("baseline uses distinct explicit sources and target; software scanline order may matter for shifted overlap; no snapshot equivalence inferred") << '}';
}
bool SameOperation(const TVPLayerOperation& a, const TVPLayerOperation& b) {
    return a.kind == b.kind && a.opacity == b.opacity && a.color == b.color &&
           a.flags == b.flags && a.phase == b.phase && a.vague == b.vague &&
           a.gammaLUT == b.gammaLUT;
}
// Pure Metal stats getter; deliberately never call the consuming software
// GetRenderStat API while inspecting capability metadata.
std::vector<uint64_t> Stats() {
    const auto s = TVPGetMetalLayerRenderStats();
    std::vector<uint64_t> result = {s.gpuOperations, s.cpuFallbacks, s.uploadedBytes,
        s.readbackBytes, s.gpuResidentBytes, s.cpuCacheBytes, s.pinnedCPUTextures,
        s.pointCacheHits, s.pointCacheMisses, s.gammaLUTUploads, s.gammaLUTUploadedBytes,
        s.psTableUploads, s.psTableUploadedBytes};
    for (auto v : s.readbackBytesBySource) result.push_back(v);
    for (auto v : s.readbackCountBySource) result.push_back(v);
    for (auto v : s.fallbackReadbackBytesByRole) result.push_back(v);
    for (auto v : s.fallbackReadbackCountByRole) result.push_back(v);
    for (auto v : s.gpuRejectCountByReason) result.push_back(v);
    return result;
}
struct MethodState {
    TVPRenderMethodRegistration registration;
    std::string objectName;
    TVPLayerOperation operation;
    bool describable;
    uint64_t gammaVersion;
    std::vector<uint8_t> gammaBytes;
};
std::vector<MethodState> Snapshot(iTVPRenderManager* manager) {
    std::vector<MethodState> result;
    for (const auto& r : manager->GetRenderMethodRegistrations()) {
        TVPLayerOperation op;
        const bool available = r.method->DescribeGpuOperation(op);
        std::vector<uint8_t> bytes;
        if(op.gammaLUT) bytes.assign(op.gammaLUT->bytes.begin(),op.gammaLUT->bytes.end());
        result.push_back({r, const_cast<iTVPRenderMethod*>(r.method)->GetName(), op, available,
                          op.gammaLUT ? op.gammaLUT->version : 0, std::move(bytes)});
    }
    return result;
}
void SameSnapshot(const std::vector<MethodState>& before, const std::vector<MethodState>& after) {
    Require(before.size() == after.size(), "audit changed registration count");
    for (size_t i = 0; i < before.size(); ++i) {
        const auto& a = before[i]; const auto& b = after[i];
        Require(a.registration.name == b.registration.name &&
                a.registration.canonicalName == b.registration.canonicalName &&
                a.registration.method == b.registration.method && a.objectName == b.objectName &&
                a.describable == b.describable && SameOperation(a.operation, b.operation) &&
                a.gammaVersion == b.gammaVersion && a.gammaBytes == b.gammaBytes,
                "audit changed method name, identity or descriptor parameters: " + a.registration.name);
    }
}
} // namespace

std::string TVPTestCapabilityAuditJSON(bool nativeCompiled, bool backendAvailable, bool deviceDouble) {
    const auto* manager = TVPGetSoftwareRenderManager();
    const auto registrations = manager->GetRenderMethodRegistrations();
    unsigned registered = 0, described = 0, gaps = 0, unknown = 0;
    for (const auto& c : capabilities) {
        const auto* method = manager->FindRegisteredRenderMethod(c.name);
        if (!method) { ++unknown; continue; }
        ++registered;
        TVPLayerOperation op;
        method->DescribeGpuOperation(op);
        if (op.kind != TVPLayerOperationKind::Unsupported) ++described;
        else ++gaps;
    }
    std::ostringstream out;
    out << "{\"schemaVersion\":1,\"backend\":{\"nativeCompiled\":" << Bool(nativeCompiled)
        << ",\"backendAvailable\":" << Bool(backendAvailable) << ",\"deviceDouble\":" << Bool(deviceDouble)
        << "},\"summary\":{\"appendixNames\":85,\"registeredNames\":" << registered
        << ",\"descriptorNames\":" << described << ",\"softwareOnlyNames\":" << gaps
        << ",\"unknownNames\":" << unknown << "},\"capabilities\":[";
    bool comma = false;
    for (const auto& c : capabilities) {
        if (comma) out << ',';
        comma = true;
        const auto* method = manager->FindRegisteredRenderMethod(c.name);
        const auto registration = std::find_if(registrations.begin(), registrations.end(),
            [&](const TVPRenderMethodRegistration& r) { return r.name == c.name; });
        out << "{\"name\":" << Quote(c.name) << ",\"registered\":" << Bool(method != nullptr);
        if (!method) {
            out << ",\"canonicalName\":null,\"status\":\"unknown; historical extension unregistered\",\"hasDescriptor\":false,\"kind\":null,\"parameters\":null,\"gpuContract\":null,\"softwareContract\":null}";
            continue;
        }
        TVPLayerOperation op;
        const bool available = method->DescribeGpuOperation(op);
        const auto* traits = TVPGetLayerOperationTraits(op.kind);
        out << ",\"canonicalName\":" << Quote(registration != registrations.end() ? registration->canonicalName : "")
            << ",\"aliasOf\":" << (std::strcmp(c.name, c.canonical) ? Quote(c.canonical) : "null")
            << ",\"status\":" << Quote(traits ? "descriptor mapped; execution domain constrained" : "software only; no GPU descriptor")
            << ",\"hasDescriptor\":" << Bool(traits != nullptr)
            << ",\"descriptorAvailableForCurrentParameters\":" << Bool(available)
            << ",\"kind\":" << Quote(traits ? traits->name : "Unsupported")
            << ",\"kindID\":" << static_cast<uint32_t>(op.kind)
            << ",\"flags\":" << op.flags
            << ",\"gammaSnapshot\":{\"available\":" << Bool(bool(op.gammaLUT))
            << ",\"byteCount\":" << (op.gammaLUT ? op.gammaLUT->bytes.size() : 0)
            << ",\"version\":" << (op.gammaLUT ? op.gammaLUT->version : 0)
            << "},\"parameters\":";
        WriteParameters(out, c);
        out << ",\"gpuContract\":";
        WriteGpuContract(out, op);
        out << ",\"softwareContract\":";
        WriteSoftwareContract(out, Software(c.name));
        out << '}';
    }
    out << "]}";
    return out.str();
}

void CapabilityAuditTests(bool checkFacade) {
    auto* manager = TVPGetSoftwareRenderManager();
    const auto before = Snapshot(manager);
    const auto stats = Stats();
    const unsigned messages = TVPTestMessageBoxes;
    Require(before.size() == 70, "expected 70 software registrations");
    std::set<std::string> expectedNames, actualNames, fixtureNames;
    unsigned described = 0, gaps = 0, unknown = 0, aliases = 0;
    for (const auto& state : before) actualNames.insert(state.registration.name);
    for (const auto& c : capabilities) {
        Require(fixtureNames.insert(c.name).second, "duplicate fixture name");
        const auto* method = manager->FindRegisteredRenderMethod(c.name);
        Require((method != nullptr) == c.registered, std::string("registration differs: ") + c.name);
        if (!c.registered) { ++unknown; continue; }
        expectedNames.insert(c.name);
        const auto it = std::find_if(before.begin(), before.end(), [&](const MethodState& s) {
            return s.registration.name == c.name;
        });
        Require(it != before.end() && it->registration.method == method,
                std::string("snapshot identity differs: ") + c.name);
        Require(it->registration.canonicalName == c.canonical && it->objectName == c.canonical,
                std::string("canonical name differs: ") + c.name);
        Require(it->operation.kind == c.kind && it->operation.flags == c.flags,
                std::string("operation mapping differs: ") + c.name);
        if (c.kind == TVPLayerOperationKind::Unsupported) {
            ++gaps;
            Require(Software(c.name) != nullptr, std::string("missing software contract: ") + c.name);
        } else { ++described; Require(TVPGetLayerOperationTraits(c.kind), "missing operation traits"); }
        if (std::strcmp(c.name, c.canonical)) {
            ++aliases;
            Require(method == manager->FindRegisteredRenderMethod(c.canonical), "alias does not share object");
        }
        // Read-only parameter enumeration verifies the checked-in ID contract.
        // Audit generation itself only reads const registration/descriptor APIs.
        for (const auto& p : Parameters(c))
            Require(const_cast<iTVPRenderMethod*>(method)->EnumParameterID(p.name.c_str()) == p.id,
                    std::string("parameter ID differs: ") + c.name + "/" + p.name);
        if (checkFacade) Require(TVPGetRenderManager()->GetRenderMethod(c.name) == method,
                                 std::string("facade object differs: ") + c.name);
    }
    Require(expectedNames == actualNames, "stable registered name set differs");
    Require(described == 70 && gaps == 0 && unknown == 15 && aliases == 6,
            "expected 70 descriptor names, zero software gaps, 15 unknown names and six alias pairs");
    Require(manager->FindRegisteredRenderMethod("ConstAlphaBlend_SD_a") !=
            manager->FindRegisteredRenderMethod("ConstAlphaBlend_SD"), "SD_a must retain independent object");
    Require(manager->FindRegisteredRenderMethod("ConstAlphaBlend_HDA") !=
            manager->FindRegisteredRenderMethod("ConstAlphaBlend"), "ConstAlpha HDA must retain independent object");
    Require(manager->FindRegisteredRenderMethod("MulBlend_HDA") !=
            manager->FindRegisteredRenderMethod("MulBlend"), "Mul HDA must retain independent object");
    Require(!manager->FindRegisteredRenderMethod(nullptr) && !manager->FindRegisteredRenderMethod("") &&
            !manager->FindRegisteredRenderMethod("__P0_unknown_capability__"), "silent lookup should reject missing names");
    const auto first = TVPTestCapabilityAuditJSON(false, checkFacade, true);
    const auto second = TVPTestCapabilityAuditJSON(false, checkFacade, true);
    Require(first == second, "repeated JSON output differs");
    SameSnapshot(before, Snapshot(manager));
    Require(stats == Stats(), "audit changed render stats");
    Require(messages == TVPTestMessageBoxes, "audit displayed an unsupported-method message");
}
