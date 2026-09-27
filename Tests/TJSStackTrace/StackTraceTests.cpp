#include "tjsCommHead.h"
#include "tjsDebug.h"
#include "tjsObject.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

using namespace TJS;
void TVPConsoleLog(const tjs_char*, ...) {}
tjs_uint64 TVPGetRoughTickCount()
{
    return static_cast<tjs_uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

namespace {
using DiagnosticCallback = void (*)();
std::atomic<DiagnosticCallback> diagnosticCallback{nullptr};
void enabledCallback() {}
bool onVMThread = true;
bool diagnosticScriptOnVMThread() { return onVMThread; }
#include "ProductionTraceOwner.inc"

void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
void assertReset()
{
    require(!TJSStackTracerEnabled() && !TJSObjectHashMapEnabled() && !TJSEnableDebugMode,
            "diagnostic tracing left VM debug state enabled");
    require(diagnosticScriptTrace.hostDepth == 0 && !diagnosticScriptTrace.sessionActive &&
            !diagnosticScriptTrace.ownsReference, "diagnostic tracer owner did not reset");
}

enum class CallbackAction { Capture, Enable, Disable };
class TraceProbe : public tTJSDispatch {
    std::vector<std::string>& traces_;
    CallbackAction& action_;
public:
    TraceProbe(std::vector<std::string>& traces, CallbackAction& action) : traces_(traces), action_(action) {}
    tjs_error FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant*,
                      tjs_int, tTJSVariant**, iTJSDispatch2*) override {
        if (action_ == CallbackAction::Enable) diagnosticCallback.store(enabledCallback);
        if (action_ == CallbackAction::Disable) diagnosticCallback.store(nullptr);
        DiagnosticScriptTraceScope nestedHostCall;
        traces_.push_back(TJSGetStackTraceString(3, " | ").AsStdString());
        return TJS_S_OK;
    }
};
void installProbe(tTJS& vm, std::vector<std::string>& traces, CallbackAction& action)
{
    auto* probe = new TraceProbe(traces, action);
    tTJSVariant value(probe); probe->Release();
    auto* global = vm.GetGlobalNoAddRef();
    require(TJS_SUCCEEDED(global->PropSet(TJS_MEMBERENSURE, "traceProbe", nullptr, &value, global)), "probe registration failed");
}
void callScript(tTJS& vm, bool throwAfter = false)
{
    const ttstr name("trace-fixture.tjs");
    vm.ExecScript(throwAfter ?
        "function inner() { try { traceProbe(); throw 'expected'; } catch(e) { throw e; } }\n"
        "function outer() { inner(); }\nouter();" :
        "function inner() { try { traceProbe(); } catch(e) { throw e; } }\n"
        "function outer() { inner(); }\nouter();", nullptr, nullptr, &name);
}
void enabledReleaseSession()
{
    assertReset();
    diagnosticCallback.store(enabledCallback);
    {
        DiagnosticScriptTraceScope start;
        require(TJSStackTracerEnabled(), "recording did not acquire tracer before VM startup");
        start.BeginSession();
        std::vector<std::string> traces;
        CallbackAction action = CallbackAction::Capture;
        {
            tTJS vm;
            installProbe(vm, traces, action);
            callScript(vm);
            require(traces.size() == 1 && traces.back().find("trace-fixture.tjs(1)") != std::string::npos &&
                    traces.back().find("inner") != std::string::npos && traces.back().find("outer") != std::string::npos,
                    "Release VM did not produce script/function/line attribution");
            require(TJSGetStackTraceString().IsEmpty(), "completed script left tracer frames");
            require(!TJSEnableDebugMode && !TJSObjectHashMapEnabled(), "stack attribution enabled unrelated debug work");
            action = CallbackAction::Disable;
            bool threw = false;
            try { callScript(vm, true); } catch (...) { threw = true; }
            require(threw && TJSStackTracerEnabled() && TJSGetStackTraceString().IsEmpty(),
                    "disable during native callback unbalanced exception unwind");
        }
        // Shutdown and script finalizers finish before the diagnostic ref is
        // released; the surrounding host boundary has not unwound yet.
        endDiagnosticScriptTraceSession();
        require(TJSStackTracerEnabled(), "teardown released tracer inside active host scope");
    }
    assertReset();
}
void enableInsideCallbackIsDeferred()
{
    assertReset();
    diagnosticCallback.store(nullptr);
    std::vector<std::string> traces;
    CallbackAction action = CallbackAction::Enable;
    std::unique_ptr<tTJS> vm;
    {
        DiagnosticScriptTraceScope start;
        start.BeginSession();
        vm = std::make_unique<tTJS>();
        installProbe(*vm, traces, action);
        callScript(*vm);
        require(traces.back().empty() && !TJSStackTracerEnabled(), "nested callback enabled tracer mid-script");
    }
    {
        DiagnosticScriptTraceScope step;
        require(TJSStackTracerEnabled(), "next outer step did not enable requested tracer");
        action = CallbackAction::Capture;
        callScript(*vm);
        require(!traces.back().empty() && TJSGetStackTraceString().IsEmpty(), "deferred trace capture is unbalanced");
        diagnosticCallback.store(nullptr);
    }
    {
        DiagnosticScriptTraceScope disabledStep;
        require(TJSStackTracerEnabled(), "disabled log collection prematurely released live-session tracer");
        vm.reset();
        endDiagnosticScriptTraceSession();
    }
    assertReset();
}
void nestedRestartAndExternalOwner()
{
    assertReset();
    diagnosticCallback.store(enabledCallback);
    TJSAddRefStackTracer(); // independent debugger/other owner
    {
        DiagnosticScriptTraceScope outer;
        outer.BeginSession();
        { tTJS first; }
        endDiagnosticScriptTraceSession();
        {
            DiagnosticScriptTraceScope completionCallbackRestart;
            completionCallbackRestart.BeginSession();
            { tTJS second; }
        }
        require(diagnosticScriptTrace.sessionActive && diagnosticScriptTrace.ownsReference,
                "nested restart lost diagnostic ownership");
        endDiagnosticScriptTraceSession();
    }
    require(TJSStackTracerEnabled() && !diagnosticScriptTrace.ownsReference,
            "diagnostic teardown released another owner's reference");
    TJSReleaseStackTracer();
    assertReset();
    onVMThread = false;
    {
        DiagnosticScriptTraceScope offThread;
        offThread.BeginSession(); endDiagnosticScriptTraceSession();
        require(!TJSStackTracerEnabled() && diagnosticScriptTrace.hostDepth == 0,
                "off-thread callback touched the VM tracer");
    }
    onVMThread = true;
    assertReset();
    // Failed host startup before BeginSession must not retain a tracer either.
    try { DiagnosticScriptTraceScope failedStart; throw std::runtime_error("startup"); }
    catch (const std::runtime_error&) {}
    assertReset();
}

class BytecodeStream : public tTJSBinaryStream {
public:
    std::vector<uint8_t> bytes;
    size_t cursor = 0;
    tjs_uint64 Seek(tjs_int64 offset, tjs_int whence) override {
        const auto base = whence == TJS_BS_SEEK_CUR ? cursor : whence == TJS_BS_SEEK_END ? bytes.size() : 0;
        const int64_t next = static_cast<int64_t>(base) + offset;
        if (next >= 0 && static_cast<uint64_t>(next) <= bytes.size()) cursor = static_cast<size_t>(next);
        return cursor;
    }
    tjs_uint Read(void* data, tjs_uint size) override {
        size = static_cast<tjs_uint>(std::min<size_t>(size, bytes.size() - cursor));
        if (size) std::memcpy(data, bytes.data() + cursor, size);
        cursor += size; return size;
    }
    tjs_uint Write(const void* data, tjs_uint size) override {
        if (cursor + size > bytes.size()) bytes.resize(cursor + size);
        if (size) std::memcpy(bytes.data() + cursor, data, size);
        cursor += size; return size;
    }
    bool Flush() override { return true; }
    tjs_uint64 GetSize() override { return bytes.size(); }
};
void strippedBytecodeHasUnverifiedPosition()
{
    assertReset();
    diagnosticCallback.store(enabledCallback);
    {
        DiagnosticScriptTraceScope scope;
        scope.BeginSession();
        {
            tTJS vm;
            std::vector<std::string> traces;
            CallbackAction action = CallbackAction::Capture;
            installProbe(vm, traces, action);
            BytecodeStream stream;
            vm.CompileScript("\n\n\nfunction bytecodeCaller() { traceProbe(); }\nbytecodeCaller();",
                             &stream, false, false, false, "stripped.tjs");
            stream.SetPosition(0);
            require(vm.LoadByteCode(&stream, nullptr, nullptr, "stripped.tjs"), "bytecode load failed");
            require(traces.size() == 1 && traces.back().find("bytecodeCaller") != std::string::npos,
                    "stripped bytecode lost function attribution");
            // This location is the VM's fallback, not an exact source line.
            require(traces.back().find("stripped.tjs(1)") != std::string::npos,
                    "stripped bytecode fallback changed; review sourcePositionUnverified labeling");
        }
        endDiagnosticScriptTraceSession();
    }
    assertReset();
}
}
int main()
{
    try {
        enabledReleaseSession();
        enableInsideCallbackIsDeferred();
        nestedRestartAndExternalOwner();
        strippedBytecodeHasUnverifiedPosition();
        std::cout << "PASS: Release stack attribution, deferred nested toggles, exception unwind, "
                     "session restart/ref ownership, off-thread exclusion and stripped-bytecode fallback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    } catch (...) {
        std::cerr << "FAIL: unexpected TJS exception\n"; return 1;
    }
}
