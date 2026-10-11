// Include the production execution unit so the VM's internal inline register
// allocator can be exercised without changing its public/exported interface.
#include "tjsInterCodeExec.cpp"
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <memory>
#include <map>
#include <vector>

void TVPConsoleLog(const tjs_char* format, ...)
{
    (void)format;
}

tjs_uint64 TVPGetRoughTickCount()
{
    return static_cast<tjs_uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

using namespace TJS;
namespace
{
// Host filter registration and worker locking are boundaries. The real TJS
// VM, closure reference counts, XP3 decoder/cache/reset methods stay production.
struct tTJSCriticalSection {};
struct tTJSCriticalSectionHolder { explicit tTJSCriticalSectionHolder(tTJSCriticalSection&) {} };
using tTVPXP3ArchiveContentFilter = void (*)();
static bool extractionInstalled=false,contentInstalled=false;
void TVPSetXP3ArchiveExtractionFilter(void (*filter)()) { extractionInstalled=filter!=nullptr; }
void TVPSetXP3ArchiveContentFilter(tTVPXP3ArchiveContentFilter filter) { contentInstalled=filter!=nullptr; }
void TVPXP3ArchiveExtractionFilterWrapper() {}
void TVPXP3ArchiveContentFilterWrapper() {}
#include "ProductionXP3Session.inc"

class ClosureObserver : public tTJSDispatch {
    int& live_;
public:
    explicit ClosureObserver(int& live):live_(live) { ++live_; }
    ~ClosureObserver() override { --live_; }
};
XP3FilterDecoder* MakeDecoder(int& live)
{
    auto* decoder=new XP3FilterDecoder();
    auto* object=new ClosureObserver(live);
    auto* context=new ClosureObserver(live);
    {
        tTJSVariant closure(object,context);
        object->Release(); context->Release();
        decoder->ManagedDecoder=closure.AsObjectClosure();
        decoder->ManagedFilter=closure.AsObjectClosure();
    }
    decoder->ScriptEngine->ExecScript(TJS_N("var decoderMarker=200; function decodeProbe() { return decoderMarker+1; }"));
    return decoder;
}
void XP3SessionLifetime()
{
    int live=0;
    for(int session=0;session<50;++session) {
        tTJS game;
        game.ExecScript(TJS_N("var gameMarker=100; function gameProbe() { return gameMarker+1; }"));
        TVPSetXP3FilterScript(TJS_N("filter A"));
        _thread_decoders.emplace(1,MakeDecoder(live));
        _cached_decoders.push_back(MakeDecoder(live));
        _ManagedDecoderInited=_ManagedFilterInited=true;
        if(live!=4) throw std::runtime_error("XP3 fixture did not retain callback/context references");
        TVPSetXP3FilterScript(TJS_N("filter B"));
        if(live || !_thread_decoders.empty() || !_cached_decoders.empty() ||
           _ManagedDecoderInited || _ManagedFilterInited || !extractionInstalled || !contentInstalled)
            throw std::runtime_error("changing XP3 script retained old active/idle decoder state");
        tTJSVariant result;
        game.EvalExpression(TJS_N("gameProbe()"),&result);
        if(result.AsInteger()!=101) throw std::runtime_error("XP3 script replacement destroyed main game VM");
        _thread_decoders.emplace(2,MakeDecoder(live));
        _cached_decoders.push_back(MakeDecoder(live));
        _ManagedDecoderInited=_ManagedFilterInited=true;
        TVPResetXP3FilterSession(); TVPResetXP3FilterSession();
        if(live || !_thread_decoders.empty() || !_cached_decoders.empty() ||
           _ManagedDecoderInited || _ManagedFilterInited || !sXP3FilterScript.IsEmpty() ||
           extractionInstalled || contentInstalled)
            throw std::runtime_error("XP3 session reset retained decoder/script/filter state");
        game.EvalExpression(TJS_N("gameProbe()"),&result);
        if(result.AsInteger()!=101) throw std::runtime_error("XP3 session reset destroyed main game VM");
    }
}

class FinalizerObserver : public tTJSDispatch
{
    int& calls_;
    int marker_;
public:
    FinalizerObserver(int& calls, int marker) : calls_(calls), marker_(marker) {}
    tjs_error FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant*,
                      tjs_int count, tTJSVariant** args, iTJSDispatch2*) override
    {
        if (count != 1 || args[0]->AsInteger() != marker_)
            throw std::runtime_error("finalizer lost the live global environment");
        ++calls_;
        return TJS_S_OK;
    }
};

void Session(int marker, bool collectBeforeShutdown)
{
    int calls = 0;
    auto* observer = new FinalizerObserver(calls, marker);
    auto* vm = new tTJS();
    auto* global = vm->GetGlobalNoAddRef();
    {
        tTJSVariant callback(observer);
        global->PropSet(TJS_MEMBERENSURE, TJS_N("observeFinalize"), nullptr, &callback, global);
    }
    tTJSVariant value(static_cast<tjs_int64>(marker));
    global->PropSet(TJS_MEMBERENSURE, TJS_N("shutdownMarker"), nullptr, &value, global);
    vm->ExecScript(TJS_N(
        "class ExitProbe {"
        "  function finalize() {"
        "    observeFinalize(shutdownMarker);"
        "    var reentrant = new Dictionary(); reentrant.value = shutdownMarker;"
        "  }"
        "}"
        "var heldProbe = new ExitProbe();"));
    // Clear incidental temporaries, then leave the last reference in a returned
    // register block, just as functions in real games do. The finalizer must
    // read globals and allocate registers while shutdown drains that block.
    auto* stack = vm->GetVariantArrayStack();
    stack->Compact();
    auto* outer = stack->Allocate(1000);
    auto* inner = stack->Allocate(1000);
    outer[0] = static_cast<tjs_int64>(marker);
    inner[0] = static_cast<tjs_int64>(marker + 1);
    vm->DoGarbageCollection(); // Compact with multiple active register blocks.
    if (outer[0].AsInteger() != marker || inner[0].AsInteger() != marker + 1)
        throw std::runtime_error("GC corrupted active registers");
    stack->Deallocate(1000, inner);
    stack->Deallocate(1000, outer);
    tTJSVariant probe;
    global->PropGet(0, TJS_N("heldProbe"), nullptr, &probe, global);
    auto* registers = stack->Allocate(8);
    registers[0] = probe;
    probe.Clear();
    stack->Deallocate(8, registers);
    global->DeleteMember(0, TJS_N("heldProbe"), nullptr, global);
    if (calls != 0) throw std::runtime_error("probe was not retained by the register pool");
    if (collectBeforeShutdown) {
        vm->DoGarbageCollection();
        if (calls != 1) throw std::runtime_error("GC did not release inactive register references");
    }
    delete vm;
    observer->Release();
    if (calls != 1) throw std::runtime_error("finalizer must run exactly once during shutdown");
}

void OverlappingEngines(bool deleteGameFirst)
{
    auto game=std::make_unique<tTJS>();
    auto decoder=std::make_unique<tTJS>();
    game->ExecScript(TJS_N("var value=100; function probe() { var d=new Dictionary(); d.value=value+1; return d.value; }"));
    decoder->ExecScript(TJS_N("var value=200; function probe() { var a=new Array(); a.add(value+1); return a[0]; }"));
    tTJSVariant result;
    if(deleteGameFirst) {
        game.reset();
        decoder->EvalExpression(TJS_N("probe()"),&result);
        if(result.AsInteger()!=201) throw std::runtime_error("closing game destroyed live decoder objects");
        decoder->ExecScript(TJS_N("value=300;"));
        decoder->EvalExpression(TJS_N("probe()"),&result);
        if(result.AsInteger()!=301) throw std::runtime_error("decoder globals did not survive game teardown");
    } else {
        decoder.reset();
        game->EvalExpression(TJS_N("probe()"),&result);
        if(result.AsInteger()!=101) throw std::runtime_error("closing decoder destroyed live game objects");
        game->ExecScript(TJS_N("value=400;"));
        game->EvalExpression(TJS_N("probe()"),&result);
        if(result.AsInteger()!=401) throw std::runtime_error("game globals did not survive decoder teardown");
    }
}
}
void RunByteCodeCompatibilityTests();
int RunByteCodeCompatibilityStaticTest(const char* path);
int main(int argc,char** argv)
{
    try {
        if(argc==3 && std::string(argv[1])=="--bytecode-static-compat")
            return RunByteCodeCompatibilityStaticTest(argv[2]);
        RunByteCodeCompatibilityTests();
        delete new tTJS(); // No script/register allocation: null pool free is safe.
        XP3SessionLifetime();
        for (int i = 0; i < 25; ++i) {
            OverlappingEngines(false);
            OverlappingEngines(true);
            Session(100 + i, false);
            Session(200 + i, true);
        }
        std::cout << "PASS: 50 XP3 cache/closure reset sessions, 50 TJS shutdown/GC sessions, "
                     "50 overlapping game/decoder lifetimes in both exit orders, "
                     "empty VM and reentrant global-access finalizers\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
