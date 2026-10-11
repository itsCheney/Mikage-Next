#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "psbfile/PSBFile.h"
#include "UtilStreams.h"
#include "../../Engine/KRKRRuntime/Source/cpp/core/render/CPUFrameDiagnostics.h"
#include <zlib.h>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <utility>

using namespace TJS;
namespace cpu = krkrsdl3::cpu_frame;
void TVPConsoleLog(const tjs_char*, ...) {}
tjs_uint64 TVPGetRoughTickCount() { return 1; }

// Only external allocation-error reporting is doubled. Memory allocation,
// copying, seeking and inputReads below execute the exact production methods.
namespace {
const char* TVPInsufficientMemory = "test allocation failure";
const char* TVPWriteError = "test stream write failure";
[[noreturn]] void TVPThrowExceptionMessage(const char* text) { throw std::runtime_error(text); }
}
#include "ProductionMemoryStream.inc"

// The shared fixture's unused Emote attach helper needs this declaration.
// It does not replace any PSBFile code or the real TJS container/VM classes.
namespace emoteplayer {
struct emotefile {
    tTJSBinaryStream* filePtr;
    PSB::PSBHeader _header;
    std::vector<std::string> namesCache;
    std::vector<uint32_t> stringsOffset;
};
}
namespace {
#include "../EmoteMetadata/PSBFixture.hpp"
uint64_t timeNS = 1;
unsigned opens = 0, inputReads = 0, streamDeaths = 0, markerDeaths = 0, markerGetters = 0;
bool failOpen = false, nullOpen = false, failRead = false;
Bytes input;
std::vector<std::string> behavior, diagnostics;
class InputStream : public TrackingStream {
public:
    explicit InputStream(Bytes bytes) : TrackingStream(std::move(bytes)) {}
    ~InputStream() override { ++streamDeaths; behavior.push_back("stream.destroy"); }
    tjs_uint Read(void* buffer, tjs_uint size) override {
        ++inputReads; behavior.push_back("read:" + std::to_string(size)); timeNS += 100;
        if (failRead) throw std::runtime_error("expected input read failure");
        return TrackingStream::Read(buffer, size);
    }
};
class DeathMarker : public tTJSDispatch {
public:
    ~DeathMarker() override { ++markerDeaths; timeNS += 20000000; behavior.push_back("tree.destroy"); }
    tjs_error PropGet(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant*, iTJSDispatch2*) override {
        ++markerGetters; return TJS_E_FAIL;
    }
};
class Trace : public tTJSDispatch {
public:
    tjs_error FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant*, tjs_int count,
                      tTJSVariant** params, iTJSDispatch2*) override {
        require(count == 1, "trace arity changed");
        behavior.push_back(ttstr(*params[0]).AsStdString()); return TJS_S_OK;
    }
};
void put(iTJSDispatch2* object, const char* name, const tTJSVariant& value) {
    require(TJS_SUCCEEDED(object->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP,
        name, nullptr, &value, object)), "property installation failed");
}
tTJSVariant get(iTJSDispatch2* object, const char* name) {
    tTJSVariant value;
    require(TJS_SUCCEEDED(object->PropGet(0, name, nullptr, &value, object)), "property lookup failed");
    return value;
}
void setMode(bool on) {
    timeNS = 1; opens = inputReads = streamDeaths = markerDeaths = markerGetters = 0;
    failOpen = nullOpen = failRead = false; behavior.clear(); diagnostics.clear();
    cpu::clockNS = [](){ return timeNS++; };
    cpu::rawLine = [](const char* line){ diagnostics.emplace_back(line); };
    cpu::captureStack = [](char* out, size_t bytes, unsigned){
        const char* stack = "fixture.tjs:1[loadScene]";
        std::snprintf(out, bytes, "%s", stack); return true;
    };
    cpu::SetEnabled(on, on ? 1 : 2, 1);
}
const cpu::Metric& metric(cpu::Kind kind) { return cpu::window.metrics[size_t(kind)]; }
Bytes scene() {
    Fixture fixture;
    return fixture.buildFullFile(Node::object({
        {"hash", Node::text("test-hash")},
        {"scenes", Node::list({
            Node::object({{"label", Node::text("start")}, {"firstLine", Node::number(1)},
                          {"lines", Node::list({Node::text("first"), Node::text("second")})}}),
            Node::object({{"label", Node::text("next")}, {"firstLine", Node::number(3)}})
        })}
    }));
}
Bytes mdf(const Bytes& plain) {
    uLongf length = compressBound(plain.size()); Bytes compressed(length);
    require(compress2(compressed.data(), &length, plain.data(), plain.size(), Z_BEST_SPEED) == Z_OK,
            "fixture compression failed");
    compressed.resize(length);
    Bytes result{'M', 'D', 'F', 0}; integer(result, plain.size(), 4); append(result, compressed);
    return result;
}
void install(tTJS& vm) {
    auto* global = vm.GetGlobalNoAddRef();
    auto* nativeClass = TVPCreateNativeClass_PsbFile();
    put(global, "PSBFile", tTJSVariant(nativeClass)); nativeClass->Release();
    auto* trace = new Trace; put(global, "trace", tTJSVariant(trace)); trace->Release();
}
void checkContent(tTJS& vm) {
    tTJSVariant result;
    vm.ExecScript("var r=p.root; return r.hash+':'+r.scenes.count+':'+r.scenes[0].lines[1]+':'+r.scenes[1].label;",
                  &result, nullptr, "content.tjs");
    require(ttstr(result).AsStdString() == "test-hash:2:second:next", "PSB content changed");
    vm.ExecScript("r=void;");
}
std::vector<std::string> lifecycle(bool on, bool compressed) {
    setMode(on); const auto plain = scene(); input = compressed ? mdf(plain) : plain;
    tTJS vm; install(vm);
    {
        cpu::StepScope step;
        vm.ExecScript("var p=new PSBFile('fixture.scn');", nullptr, nullptr, "scene.tjs");
        checkContent(vm);
        // Production VM register blocks keep temporary variants until compact.
        // Release root-getter temporaries while the explicit owner is live.
        vm.DoGarbageCollection();
        auto owner = get(vm.GetGlobalNoAddRef(), "p");
        auto root = get(owner.AsObjectNoAddRef(), "root");
        auto* marker = new DeathMarker;
        put(root.AsObjectNoAddRef(), "deathProbe", tTJSVariant(marker)); marker->Release();
        root.Clear(); owner.Clear();
        vm.ExecScript("invalidate p; p=void;", nullptr, nullptr, "clear.tjs");
        vm.DoGarbageCollection();
    }
    if (!(opens == 1 && streamDeaths == 1 && markerDeaths == 1 && markerGetters == 0))
        std::cerr << "lifecycle on=" << on << " compressed=" << compressed << " opens=" << opens
                  << " streams=" << streamDeaths << " markers=" << markerDeaths << " getters=" << markerGetters
                  << " destroy=" << metric(cpu::Kind::PSBDestroy).calls << " invalidate=" << metric(cpu::Kind::PSBInvalidate).calls << '\n';
    require(opens == 1 && streamDeaths == 1 && markerDeaths == 1 && markerGetters == 0,
            "diagnostics changed IO, ownership or property invocation count");
    if (on) {
        require(metric(cpu::Kind::PSBLoad).calls == 1 && metric(cpu::Kind::PSBOpen).calls == 1,
                "PSB load/open scope not singular");
        require(metric(cpu::Kind::PSBTables).calls == 1 && metric(cpu::Kind::PSBMaterialize).calls == 1,
                "PSB tables/materialization scope not singular");
        require(metric(cpu::Kind::PSBInvalidate).calls == 1 && metric(cpu::Kind::PSBDestroy).calls == 1,
                "PSB invalidation/destruction scope missing");
        require(metric(cpu::Kind::PSBDestroy).wallNS >= 20000000,
                "destroy scope did not include _root member destruction");
        require(metric(cpu::Kind::PSBRead).calls == (compressed ? 3 : 2), "PSB read replayed");
        require(metric(cpu::Kind::PSBDecompress).calls == unsigned(compressed) &&
                metric(cpu::Kind::PSBCopy).calls == unsigned(compressed), "PSB decode/copy path incorrect");
        require(metric(cpu::Kind::PSBLoad).workBytes == input.size(), "PSB load byte accounting wrong");
        require(metric(cpu::Kind::PSBRead).workBytes == input.size() + (compressed ? 0 : 4),
                "PSB read byte accounting wrong");
        if (compressed) require(metric(cpu::Kind::PSBCopy).workBytes == plain.size() &&
            metric(cpu::Kind::PSBDecompress).workBytes == input.size() - 8, "MDF byte accounting wrong");
        cpu::TakeWindow(1,2);
        require(std::any_of(diagnostics.begin(),diagnostics.end(),[](const auto& line){
            return line.find("cpu.slow ")==0 && line.find("kind=psb.destroy ")!=std::string::npos;
        }), "production owner destruction did not survive bounded detail selection");
        for(const auto& line:diagnostics)require(line.size()<=960,"PSB diagnostic line exceeded budget");
    } else {
        for (const auto& m : cpu::window.metrics) require(m.calls == 0, "disabled instrumentation recorded work");
    }
    return behavior;
}
void finalizerOrdering(bool on) {
    setMode(on); input = scene(); tTJS vm; install(vm);
    vm.ExecScript(
        "class StorageData { var data; function StorageData(){data=new PSBFile('fixture.scn');}"
        "function finalize(){trace('script.finalize'); invalidate data; trace('script.afterInvalidate');}}"
        "var holder=new StorageData(); invalidate holder; holder=void;",
        nullptr, nullptr, "StorageData.tjs");
    vm.DoGarbageCollection();
    auto first = std::find(behavior.begin(), behavior.end(), "script.finalize");
    auto after = std::find(behavior.begin(), behavior.end(), "script.afterInvalidate");
    require(first != behavior.end() && after != behavior.end() && first < after &&
            opens == 1 && streamDeaths == 1, "script finalizer/stream ownership changed");
    if(on) require(metric(cpu::Kind::PSBInvalidate).calls == 1 && metric(cpu::Kind::PSBDestroy).calls == 1,
                   "script finalizer did not reach native lifecycle scopes");
}

std::vector<std::string> retainedRootBoundary(bool on) {
    setMode(on); input=scene(); tTJS vm; install(vm);
    {
        cpu::StepScope step;
        vm.ExecScript("var p=new PSBFile('fixture.scn');");
        auto owner=get(vm.GetGlobalNoAddRef(),"p");
        auto retainedRoot=get(owner.AsObjectNoAddRef(),"root");
        vm.DoGarbageCollection();
        auto* marker=new DeathMarker;
        put(retainedRoot.AsObjectNoAddRef(),"deathProbe",tTJSVariant(marker));marker->Release();
        owner.Clear();
        vm.ExecScript("invalidate p; p=void;");vm.DoGarbageCollection();
        require(markerDeaths==0,"PSB owner deletion released a still-owned root tree");
        const auto measured=metric(cpu::Kind::PSBDestroy).wallNS;
        if(on)require(metric(cpu::Kind::PSBDestroy).calls==1 && measured<20000000,
                      "PSB destroy attributed externally retained tree destruction");
        retainedRoot.Clear();
        require(markerDeaths==1,"last external root release did not destroy the tree");
        if(on)require(metric(cpu::Kind::PSBDestroy).wallNS==measured,
                      "PSB destroy retroactively claimed a later external release");
    }
    require(opens==1 && streamDeaths==1 && markerGetters==0,"retained-root observation changed IO or getters");
    return behavior;
}
enum class Failure { Null, Open, Read, Short, MDF, Version };
std::vector<std::string> failure(bool on, Failure mode) {
    setMode(on); input = scene();
    nullOpen = mode == Failure::Null; failOpen = mode == Failure::Open; failRead = mode == Failure::Read;
    if (mode == Failure::Short) input = {1, 2, 3};
    if (mode == Failure::MDF) input = {'M', 'D', 'F', 0, 8, 0, 0, 0, 0, 0};
    if (mode == Failure::Version) { input[4] = 4; input[5] = 0; }
    tTJS vm; install(vm); bool thrown = false;
    vm.ExecScript("var p=void;");
    try { vm.ExecScript("p=new PSBFile('fixture.scn');", nullptr, nullptr, "failure.tjs"); }
    catch (...) { thrown = true; }
    require(thrown == (mode == Failure::Open || mode == Failure::Read), "native failure behavior changed");
    vm.ExecScript("p=void;");
    vm.DoGarbageCollection();
    require(opens == 1 && inputReads == (mode == Failure::Null || mode == Failure::Open || mode == Failure::Short ? 0 :
        mode == Failure::MDF ? 3 : mode == Failure::Version ? 2 : 1), "failure replayed input work");
    require(streamDeaths == unsigned(mode != Failure::Null && mode != Failure::Open),
            "failure changed stream ownership");
    if (on) {
        require(metric(cpu::Kind::PSBLoad).calls == unsigned(mode != Failure::Open), "failure load count wrong");
        if (mode != Failure::Open) require(metric(cpu::Kind::PSBLoad).failures == 1,
                                          "load failure was not attributed");
        if (mode == Failure::Read) require(metric(cpu::Kind::PSBRead).failures == 1,
                                          "read exception was not attributed");
        if (mode == Failure::MDF) require(metric(cpu::Kind::PSBDecompress).failures == 1 &&
            metric(cpu::Kind::PSBMaterialize).calls == 0, "MDF failure proceeded to parse");
    }
    return behavior;
}
}

// The storage boundary is the sole IO double; production PSBFile and PSBMedia
// own/delete its returned stream exactly as in the engine.
tTJSBinaryStream* TVPCreateStream(const ttstr&, tjs_uint32) {
    ++opens; behavior.push_back("open"); timeNS += 100;
    if (failOpen) throw std::runtime_error("expected open failure");
    if (nullOpen) return nullptr;
    return new InputStream(input);
}

int main() {
    try {
        require(lifecycle(false, false) == lifecycle(true, false), "plain on/off behavior differs");
        require(lifecycle(false, true) == lifecycle(true, true), "MDF on/off behavior differs");
        finalizerOrdering(false); const auto off = behavior; finalizerOrdering(true);
        require(off == behavior, "on/off script finalizer ordering differs");
        require(retainedRootBoundary(false)==retainedRootBoundary(true),
                "on/off retained-root lifetime differs");
        for (auto mode : {Failure::Null, Failure::Open, Failure::Read, Failure::Short, Failure::MDF, Failure::Version})
            require(failure(false, mode) == failure(true, mode), "on/off failure behavior differs");
        cpu::SetEnabled(false, 3, 1); cpu::clockNS = cpu::Clock; cpu::rawLine = nullptr; cpu::captureStack = nullptr;
        std::cout << "PASS: production PSB plain/MDF semantics, bounded stage metrics, native owner/tree destruction, external root lifetime, script finalizers and failures\n";
        return 0;
    } catch (const eTJSError& error) {
        std::cerr << "FAIL: " << error.GetMessage().AsStdString() << '\n';
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; }
    return 1;
}
