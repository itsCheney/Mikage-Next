// Include the production execution unit so the VM's internal inline register
// allocator can be exercised without changing its public/exported interface.
#include "tjsInterCodeExec.cpp"
#include <iostream>
#include <chrono>
#include <stdexcept>

void TVPConsoleLog(const tjs_char* format, ...)
{
    std::cerr << format << '\n';
}

tjs_uint64 TVPGetRoughTickCount()
{
    return static_cast<tjs_uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

using namespace TJS;
namespace
{
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
}
int main()
{
    try {
        delete new tTJS(); // No script/register allocation: null pool free is safe.
        for (int i = 0; i < 25; ++i) {
            Session(100 + i, false);
            Session(200 + i, true);
        }
        std::cout << "PASS: 50 TJS shutdown/GC sessions, empty VM and reentrant global-access finalizers\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
