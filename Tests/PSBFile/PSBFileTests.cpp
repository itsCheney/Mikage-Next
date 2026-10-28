#include "tjsCommHead.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "psbfile/PSBFile.h"
#include "psbfile/psbsession.h"
#include "TVPEvent.h"
#include "UtilStreams.h"
#include "../../Engine/KRKRRuntime/Source/cpp/core/render/CPUFrameDiagnostics.h"
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <utility>

using namespace TJS;
namespace cpu = krkrsdl3::cpu_frame;
void TVPConsoleLog(const tjs_char*, ...) {}
tjs_uint64 TVPGetRoughTickCount() { return 1; }
// The frame loop is the only event double: production PSBFile hooks itself to
// free released trees, and the tests run its frames.
std::vector<tTVPContinuousEventCallbackIntf*> continuousHooks;
void TVPAddContinuousEventHook(tTVPContinuousEventCallbackIntf* cb) { continuousHooks.push_back(cb); }
void TVPRemoveContinuousEventHook(tTVPContinuousEventCallbackIntf* cb) {
    continuousHooks.erase(std::remove(continuousHooks.begin(), continuousHooks.end(), cb), continuousHooks.end());
}
// Runs frames until the deferred release unhooks; returns the frame count.
int drainDeferred() {
    int frames = 0;
    while (!continuousHooks.empty() && frames < 100000) { continuousHooks.front()->OnContinuousCallback(0); ++frames; }
    return frames;
}
// Queued containers belong to their VM, so each test drains before its VM goes.
struct DrainOnExit { ~DrainOnExit() { drainDeferred(); } };

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
    tTJS vm; DrainOnExit drain; install(vm);
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
        // The owner's release leaves the tree to later frames.
        require(markerDeaths == 0, "owner release freed the tree at once");
        require(drainDeferred() >= 1 && continuousHooks.empty(), "deferred tree release did not finish");
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
        require(metric(cpu::Kind::PSBDestroy).wallNS < 20000000,
                "destroy scope still freed the tree");
        require(metric(cpu::Kind::PSBRead).calls == (compressed ? 3 : 2), "PSB read replayed");
        // MDF inflates straight into the parse buffer: no separate copy.
        require(metric(cpu::Kind::PSBDecompress).calls == unsigned(compressed) &&
                metric(cpu::Kind::PSBCopy).calls == 0, "PSB decode/copy path incorrect");
        require(metric(cpu::Kind::PSBLoad).workBytes == input.size(), "PSB load byte accounting wrong");
        require(metric(cpu::Kind::PSBRead).workBytes == input.size() + (compressed ? 0 : 4),
                "PSB read byte accounting wrong");
        if (compressed) require(metric(cpu::Kind::PSBDecompress).workBytes == input.size() - 8,
            "MDF byte accounting wrong");
        cpu::TakeWindow(1,2);
        for(const auto& line:diagnostics)require(line.size()<=960,"PSB diagnostic line exceeded budget");
    } else {
        for (const auto& m : cpu::window.metrics) require(m.calls == 0, "disabled instrumentation recorded work");
    }
    return behavior;
}
void finalizerOrdering(bool on) {
    setMode(on); input = scene(); tTJS vm; DrainOnExit drain; install(vm);
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
    setMode(on); input=scene(); tTJS vm; DrainOnExit drain; install(vm);
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
        drainDeferred();
        require(markerDeaths==0,"deferred release freed a still-owned root tree");
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
    tTJS vm; DrainOnExit drain; install(vm); bool thrown = false;
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

std::string evaluate(tTJS& vm, const char* script) {
    tTJSVariant result;
    vm.ExecScript(script, &result, nullptr, "check.tjs");
    return ttstr(result).AsStdString();
}

// N3/N5/N6/N7 sign extension and an ArrayN5 count (5 count bytes, which used
// to be read into a 4-byte variable).
void numbersAndWideArrays() {
    setMode(false); Fixture fixture;
    Bytes wide{PSB::ArrayN5}; integer(wide, 2, 5); wide.push_back(PSB::ArrayN1); wide.push_back(7); wide.push_back(9);
    input = fixture.buildFullFile(Node::object({
        {"n3", Node::number(-70000, 3)}, {"p3", Node::number(70000, 3)},
        {"n5", Node::number(-5000000000LL, 5)}, {"n6", Node::number(-100000000000000LL, 6)},
        {"n7", Node::number(-30000000000000000LL, 7)}, {"wide", Node::raw(wide)}}));
    tTJS vm; DrainOnExit drain; install(vm);
    const auto value = evaluate(vm, "var p=new PSBFile('fixture.scn'); var r=p.root;"
        "return ''+r.n3+','+r.p3+','+r.n5+','+r.n6+','+r.n7+','+r.wide.count+','+r.wide[0]+','+r.wide[1];");
    if (value != "-70000,70000,-5000000000,-100000000000000,-30000000000000000,2,7,9")
        std::cerr << "numbers: " << value << '\n';
    require(value == "-70000,70000,-5000000000,-100000000000000,-30000000000000000,2,7,9",
            "PSB number sign extension or wide array count wrong");
    vm.ExecScript("r=void; invalidate p; p=void;"); vm.DoGarbageCollection();
}

// PSB v1: names come from a name-offset table, and each object entry is a
// 32-bit name index followed by the value.
void versionOne() {
    setMode(false);
    std::vector<std::string> names;
    for (int i = 0; i < 300; ++i) names.push_back("n" + std::to_string(i));
    names[0] = "alpha"; names[1] = "beta";
    Bytes bytes(40);
    const uint32_t offsetNames = bytes.size();
    std::vector<uint32_t> nameOffsets;
    for (const auto& name : names) {
        nameOffsets.push_back(bytes.size() - offsetNames);
        bytes.insert(bytes.end(), name.begin(), name.end()); bytes.push_back(0);
    }
    const uint32_t offsetEncrypt = bytes.size(); append(bytes, packed(nameOffsets));
    const uint32_t offsetStrings = bytes.size(); append(bytes, packed({0, 4}));
    const uint32_t offsetStringsData = bytes.size(); append(bytes, Bytes{'o', 'n', 'e', 0, 'l', 'a', 's', 't', 0});
    const uint32_t offsetChunkOffsets = bytes.size(); append(bytes, packed({}));
    const uint32_t offsetChunkLengths = bytes.size(); append(bytes, packed({}));
    const uint32_t offsetChunkData = bytes.size();
    const uint32_t offsetEntries = bytes.size();
    std::vector<std::pair<uint32_t, Bytes>> members;
    Bytes one{PSB::StringN4}; integer(one, 0, 4);
    Bytes last{PSB::StringN4}; integer(last, 1, 4);
    members.push_back({0, one});
    members.push_back({1, Node::number(300, 2).scalar});
    members.push_back({299, last});
    std::vector<uint32_t> offsets; Bytes payload;
    for (const auto& member : members) {
        offsets.push_back(payload.size());
        integer(payload, member.first, 4); append(payload, member.second);
    }
    bytes.push_back(PSB::Objects); append(bytes, packed(offsets)); append(bytes, payload);
    Bytes header{'P', 'S', 'B', 0}; integer(header, 1, 2); integer(header, 0, 2);
    for (uint32_t field : {offsetEncrypt, offsetNames, offsetStrings, offsetStringsData,
                           offsetChunkOffsets, offsetChunkLengths, offsetChunkData, offsetEntries})
        integer(header, field, 4);
    std::copy(header.begin(), header.end(), bytes.begin());
    input = bytes;
    tTJS vm; DrainOnExit drain; install(vm);
    const auto value = evaluate(vm, "var p=new PSBFile('fixture.scn'); var r=p.root; return r.alpha+','+r.beta+','+r.n299;");
    if (value != "one,300,last") std::cerr << "v1: " << value << '\n';
    require(value == "one,300,last", "PSB v1 names or object entries wrong");
    vm.ExecScript("r=void; invalidate p; p=void;"); vm.DoGarbageCollection();
}

// psb:// serves the decompressed resource bytes from the octets the tree
// holds, without reopening the file, and reads the file again only after the
// PSBFile is gone.
void mediaSharing() {
    setMode(false); Fixture fixture;
    const Bytes image{'I', 'M', 'G', 1, 2, 3, 0, 255};
    fixture.chunks = {Bytes{'x', 'y'}, image};
    input = mdf(fixture.buildFullFile(Node::object({{"img.png", Node::resource(1)}, {"a.txt", Node::resource(0)}})));
    auto* media = new PSB::PSBMedia(); psbVar = media;
    auto read = [&](const char* name) {
        std::unique_ptr<tTJSBinaryStream> stream(media->Open(name, 0));
        require(stream != nullptr, "psb:// did not open a resource");
        Bytes bytes(stream->GetSize());
        if (!bytes.empty()) stream->ReadBuffer(bytes.data(), bytes.size());
        return bytes;
    };
    {
        tTJS vm; DrainOnExit drain; install(vm);
        vm.ExecScript("var p=new PSBFile('data/Fixture.scn');");
        auto owner = get(vm.GetGlobalNoAddRef(), "p");
        auto root = get(owner.AsObjectNoAddRef(), "root");
        auto octet = get(root.AsObjectNoAddRef(), "img.png");
        require(octet.Type() == tvtOctet && Bytes(octet.AsOctetNoAddRef()->GetData(),
            octet.AsOctetNoAddRef()->GetData() + octet.AsOctetNoAddRef()->GetLength()) == image,
            "PSB resource octet wrong");
        octet.Clear(); root.Clear(); owner.Clear();
        require(opens == 1, "PSB load opened the file more than once");
        require(read("fixture.scn/img.png") == image, "psb:// served MDF resource from the wrong bytes");
        require(read("FIXTURE.SCN/IMG.PNG") == image, "psb:// lookup is case-sensitive");
        require(read("fixture.scn/a.txt") == Bytes({'x', 'y'}), "psb:// served the wrong chunk");
        require(media->CheckExistentStorage("fixture.scn/a.txt") && !media->CheckExistentStorage("fixture.scn/none"),
                "psb:// existence check wrong");
        require(opens == 1, "psb:// reopened the PSB while its PSBFile was alive");
        vm.ExecScript("invalidate p; p=void;"); vm.DoGarbageCollection();
        require(read("fixture.scn/img.png") == image, "psb:// failed after the PSBFile was released");
        require(opens == 2, "psb:// did not reread the PSB after its PSBFile was released");
    }
    media->Release(); psbVar = nullptr;
}

Bytes streamBytes(tTJSBinaryStream* raw) {
    std::unique_ptr<tTJSBinaryStream> stream(raw);
    require(stream != nullptr, "psb:// did not open a resource");
    Bytes bytes(stream->GetSize());
    if (!bytes.empty()) stream->ReadBuffer(bytes.data(), bytes.size());
    return bytes;
}

// A reload that fails must not leave psb:// serving the previous file.
void failedReload() {
    for (bool throwing : {false, true}) {
        setMode(false); Fixture fixture;
        fixture.chunks = {Bytes{'x', 'y'}};
        input = fixture.buildFullFile(Node::object({{"a.txt", Node::resource(0)}}));
        auto* media = new PSB::PSBMedia(); psbVar = media;
        {
            tTJS vm; DrainOnExit drain; install(vm);
            vm.ExecScript("var p=new PSBFile('fixture.scn');");
            require(media->CheckExistentStorage("fixture.scn/a.txt"), "loaded PSB not served");
            const Bytes good = input;
            if (throwing) failRead = true; else { input[4] = 4; input[5] = 0; }
            bool thrown = false;
            try { require(evaluate(vm, "return p.load('fixture.scn')?'ok':'failed';") == "failed",
                          "corrupt reload succeeded"); }
            catch (...) { thrown = true; }
            require(thrown == throwing, "reload failure behavior changed");
            require(!media->CheckExistentStorage("fixture.scn/a.txt"),
                    "failed reload left the previous file registered");
            require(media->Open("fixture.scn/a.txt", 0) == nullptr, "failed reload still serves the previous file");
            // A later successful load registers again.
            failRead = false; input = good;
            require(evaluate(vm, "return p.load('fixture.scn')?'ok':'failed';") == "ok", "reload failed");
            const unsigned before = opens;
            require(streamBytes(media->Open("fixture.scn/a.txt", 0)) == Bytes({'x', 'y'}) && opens == before,
                    "successful reload not served from the tree");
            vm.ExecScript("invalidate p; p=void;"); vm.DoGarbageCollection();
        }
        media->Release(); psbVar = nullptr;
    }
}

// Opening several resources of a released PSB reads and inflates it once.
void releasedOpensReadOnce() {
    setMode(false); Fixture fixture;
    const Bytes image{'I', 'M', 'G', 1, 2, 3};
    fixture.chunks = {Bytes{'x', 'y'}, image};
    input = mdf(fixture.buildFullFile(Node::object({{"img.png", Node::resource(1)}, {"a.txt", Node::resource(0)}})));
    auto* media = new PSB::PSBMedia(); psbVar = media;
    {
        tTJS vm; DrainOnExit drain; install(vm);
        vm.ExecScript("var p=new PSBFile('fixture.scn'); invalidate p; p=void;"); vm.DoGarbageCollection();
        require(opens == 1, "PSB load opened the file more than once");
        require(streamBytes(media->Open("fixture.scn/img.png", 0)) == image, "released PSB resource wrong");
        require(streamBytes(media->Open("fixture.scn/a.txt", 0)) == Bytes({'x', 'y'}), "released PSB chunk wrong");
        require(streamBytes(media->Open("FIXTURE.SCN/img.png", 0)) == image, "released PSB reopen wrong");
        require(opens == 2, "each open of a released PSB read the file again");
    }
    media->Release(); psbVar = nullptr;
}

// psb:// names a PSB no PSBFile loaded: existence is answered from its
// resource list alone, without a PSBFile load or a TJS tree, and a failing
// open answers false.
void unregisteredExistence() {
    setMode(true); Fixture fixture;
    const Bytes image{'I', 'M', 'G'};
    fixture.chunks = {image};
    input = fixture.buildFullFile(Node::object({{"hash", Node::text("h")},
        {"list", Node::list({Node::number(1)})}, {"img.png", Node::resource(0)}}));
    auto* media = new PSB::PSBMedia(); psbVar = media;
    require(media->CheckExistentStorage("fixture.scn/img.png"), "unregistered PSB resource not found");
    require(!media->CheckExistentStorage("fixture.scn/none"), "unregistered PSB reported a missing resource");
    require(!media->CheckExistentStorage("fixture.scn"), "domain without a resource reported");
    require(opens == 1, "existence checks reread the PSB");
    require(metric(cpu::Kind::PSBLoad).calls == 0 && metric(cpu::Kind::PSBMaterialize).calls == 0,
            "existence check loaded a PSBFile");
    require(streamBytes(media->Open("fixture.scn/img.png", 0)) == image && opens == 1,
            "open after an existence check reread the PSB");
    {
        // A PSBFile loaded afterwards registers and serves from its tree.
        tTJS vm; DrainOnExit drain; install(vm);
        vm.ExecScript("var p=new PSBFile('fixture.scn');");
        require(opens == 2 && metric(cpu::Kind::PSBLoad).calls == 1, "PSBFile load after probe changed");
        require(streamBytes(media->Open("fixture.scn/img.png", 0)) == image && opens == 2,
                "registered PSB not served from its tree");
        vm.ExecScript("invalidate p; p=void;"); vm.DoGarbageCollection();
    }
    cpu::SetEnabled(false, 3, 1);
    for (int mode = 0; mode < 3; ++mode) {
        failOpen = mode == 0; nullOpen = mode == 1; failRead = mode == 2;
        bool answer = true;
        try { answer = media->CheckExistentStorage("other.scn/img.png"); }
        catch (...) { require(false, "existence check let a stream failure escape"); }
        require(!answer, "unreadable PSB reported a resource");
        require(media->Open("other.scn/img.png", 0) == nullptr, "unreadable PSB opened");
    }
    failOpen = nullOpen = failRead = false;
    media->Release(); psbVar = nullptr;
}

// Motion PSBs repeat member names such as "pixel"; psb:// keeps serving the
// first resource under a repeated name.
void repeatedNames() {
    setMode(false); Fixture fixture;
    fixture.chunks = {Bytes{'a'}, Bytes{'b'}};
    input = fixture.buildFullFile(Node::object({{"one", Node::object({{"pixel", Node::resource(0)}})},
                                                {"two", Node::object({{"pixel", Node::resource(1)}})}}));
    auto* media = new PSB::PSBMedia(); psbVar = media;
    {
        tTJS vm; DrainOnExit drain; install(vm);
        vm.ExecScript("var p=new PSBFile('fixture.scn');");
        std::unique_ptr<tTJSBinaryStream> stream(media->Open("fixture.scn/pixel", 0));
        require(stream != nullptr && stream->GetSize() == 1, "psb:// did not open a repeated name");
        char byte = 0; stream->ReadBuffer(&byte, 1);
        require(byte == 'a', "psb:// served a later resource for a repeated name");
        stream.reset();
        vm.ExecScript("invalidate p; p=void;"); vm.DoGarbageCollection();
    }
    media->Release(); psbVar = nullptr;
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
        numbersAndWideArrays();
        versionOne();
        mediaSharing();
        repeatedNames();
        failedReload();
        releasedOpensReadOnce();
        unregisteredExistence();
        cpu::SetEnabled(false, 3, 1); cpu::clockNS = cpu::Clock; cpu::rawLine = nullptr; cpu::captureStack = nullptr;
        std::cout << "PASS: production PSB plain/MDF semantics, number signs, wide arrays, v1 names, psb:// sharing, repeated names, failed reloads, read cache and unregistered probes, bounded stage metrics, native owner/tree destruction, external root lifetime, script finalizers and failures\n";
        return 0;
    } catch (const eTJSError& error) {
        std::cerr << "FAIL: " << error.GetMessage().AsStdString() << '\n';
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; }
    return 1;
}
