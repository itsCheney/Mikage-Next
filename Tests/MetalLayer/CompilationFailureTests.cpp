#include "tjsCommHead.h"
#include "RenderManager.h"
#include <stdexcept>
#include <string>
#include <vector>

extern bool TVPTestCaptureLogs;
extern std::vector<std::string> TVPTestLogs;
extern unsigned TVPTestMessageBoxes;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Isolate protected registration and a successful custom compiler from the
// process-lived software registry. No texture or rendering stubs are exercised.
class CompilerProbe final : public iTVPRenderManager {
    class Method final : public iTVPRenderMethod { public: ~Method() override = default; } method;
    iTVPRenderMethod* GetRenderMethodFromScript(const char*, int, unsigned) override {
        ++compileCalls;
        return compilerAvailable ? &method : nullptr;
    }
public:
    bool compilerAvailable = false;
    unsigned compileCalls = 0;
    ~CompilerProbe() override = default;
    void TryNullRegistration() { RegisterRenderMethod("p0.null", nullptr); }
    const char* GetName() override { return "CompilerProbe"; }
    bool GetRenderStat(unsigned&, uint64_t&) override { return false; }
    iTVPTexture2D* CreateTexture2D(const void*, int, unsigned, unsigned, TVPTextureFormat::e, int) override { return nullptr; }
    iTVPTexture2D* CreateTexture2D(tTVPBitmap*) override { return nullptr; }
    iTVPTexture2D* CreateTexture2D(TJS::tTJSBinaryStream*) override { return nullptr; }
    iTVPTexture2D* CreateTexture2D(unsigned, unsigned, iTVPTexture2D*) override { return nullptr; }
    void OperateRect(iTVPRenderMethod*, iTVPTexture2D*, iTVPTexture2D*, const tTVPRect&, const tRenderTexRectArray&) override {}
    void OperateTriangles(iTVPRenderMethod*, int, iTVPTexture2D*, iTVPTexture2D*, const tTVPRect&, const tTVPPointD*, const tRenderTexQuadArray&) override {}
    void OperatePerspective(iTVPRenderMethod*, int, iTVPTexture2D*, iTVPTexture2D*, const tTVPRect&, const tTVPPointD*, const tRenderTexQuadArray&) override {}
};
}

void CompilationFailureTests() {
    const bool previousCapture = TVPTestCaptureLogs;
    const auto previousLogs = TVPTestLogs;
    const auto boxes = TVPTestMessageBoxes;
    TVPTestCaptureLogs = true;
    TVPTestLogs.clear();
    auto* software = TVPGetSoftwareRenderManager();
    const auto before = software->GetRenderMethodRegistrations();
    const char* name = "p0.unsupported.dynamic.shader";
    uint32_t hint = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Require(!software->CompileRenderMethod(name, "void main() {}", 1), "unsupported compiler returned a method");
        Require(!software->GetOrCompileRenderMethod(name, &hint, "void main() {}", 1), "unsupported cached compiler returned a method");
        Require(!software->FindRegisteredRenderMethod(name), "failed dynamic compile inserted a method");
    }
    Require(hint != 0, "failed compile lost the reusable name hash");
    const auto after = software->GetRenderMethodRegistrations();
    Require(before.size() == after.size(), "failed compile changed registry size");
    for (size_t i = 0; i < before.size(); ++i)
        Require(before[i].name == after[i].name && before[i].canonicalName == after[i].canonicalName &&
                before[i].method == after[i].method, "failed compile changed canonical registry");
    Require(TVPTestLogs.size() == 6, "dynamic compile failures were not diagnosed");
    for (const auto& log : TVPTestLogs)
        Require(log.find("render.compileFailed") != std::string::npos && log.find(name) != std::string::npos &&
                log.find("backend=Software") != std::string::npos, "dynamic compile diagnostic lacks method/backend");

    CompilerProbe probe;
    probe.TryNullRegistration();
    Require(probe.GetRenderMethodRegistrations().empty() && !probe.FindRegisteredRenderMethod("p0.null"),
            "null method reached registry in Release");
    Require(!probe.CompileRenderMethod("p0.retry", "script", 1), "null compiler result registered");
    probe.compilerAvailable = true;
    auto* compiled = probe.CompileRenderMethod("p0.retry", "script", 1);
    Require(compiled && probe.FindRegisteredRenderMethod("p0.retry") == compiled, "working custom compiler no longer registers");
    hint = 0;
    Require(probe.GetOrCompileRenderMethod("p0.retry", &hint, "other script", 1) == compiled && probe.compileCalls == 2,
            "cached compiled method was recompiled");
    Require(TVPTestMessageBoxes == boxes, "compile/audit failure showed a message box");
    TVPTestCaptureLogs = previousCapture;
    TVPTestLogs = previousLogs;
}
