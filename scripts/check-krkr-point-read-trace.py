#!/usr/bin/env python3
"""Compile and exercise point-read diagnostic context without SDL, TJS or Metal.

The production header is shared by two translation units to verify that caller
context reaches the backend. Both builds keep their checks enabled, including
the optimized build with NDEBUG. This does not measure device rendering time.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent

BACKEND = r'''
#include "PointReadTrace.h"
namespace trace = krkrsdl3::point_trace;
trace::Query* backendQuery() { return trace::CurrentQuery(); }
const char* backendWriter() { return trace::CurrentWriter(); }
void backendComplete() {
    if (auto* query = trace::CurrentQuery()) {
        query->lastSubmittedID = 71;
        query->renderFrame = 92;
        query->wallNS = 19000000;
        query->gpuWaitNS = 17000000;
        query->finishedNS = 123456789;
        query->reported = true;
    }
}
'''

TESTS = r'''
#include "PointReadTrace.h"
#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace trace = krkrsdl3::point_trace;
trace::Query* backendQuery();
const char* backendWriter();
void backendComplete();

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static bool sameOrigin(const trace::Origin& a, const trace::Origin& b) {
    return a.source == b.source && a.trigger == b.trigger &&
           a.parentTrigger == b.parentTrigger && a.owner == b.owner;
}
static void requireEmpty() {
    require(sameOrigin(trace::origin, {}), "origin leaked beyond its scope");
    require(trace::query == nullptr && backendQuery() == nullptr,
            "query leaked beyond its scope or across translation units");
    require(trace::writer == nullptr && backendWriter() == nullptr,
            "writer leaked beyond its scope or across translation units");
}

static void disabledScopes() {
    trace::SetEnabled(false);
    require(!trace::Enabled(), "diagnostics should be disabled");
    requireEmpty();
    const auto queryCounter = trace::nextQueryID.load();
    const auto textureCounter = trace::nextTextureID.load();
    int owner = 0;
    trace::Query value;
    {
        trace::OriginScope origin(trace::Source::BitmapMask, &owner);
        trace::TriggerScope trigger(trace::Trigger::Click, false);
        trace::WriterScope writer("disabledWriter");
        trace::QueryScope query(value);
        requireEmpty();
        backendComplete();
        require(value.queryID == 0 && sameOrigin(value.origin, {}),
                "disabled query acquired an ID or caller context");
        require(!value.reported && value.wallNS == 0,
                "disabled query reached backend completion");
    }
    requireEmpty();
    require(trace::nextQueryID.load() == queryCounter &&
            trace::nextTextureID.load() == textureCounter,
            "disabled scopes allocated diagnostic IDs");
}

static void nestedScopesAndHandoff() {
    trace::SetEnabled(true);
    int outerOwner = 0, innerOwner = 0;
    trace::Query outer, inner;
    outer.textureID = trace::NextTextureID();
    outer.version = 12;
    outer.x = 17; outer.y = 23; outer.width = 1920; outer.height = 1080;
    outer.alphaOnly = true;
    outer.missReason = "invalidated";
    outer.invalidation = trace::Invalidation::GPUOverwrite;
    outer.writer = "emote.draw";
    outer.invalidatedVersion = 11;
    {
        trace::OriginScope source(trace::Source::LayerHitTest, &outerOwner);
        trace::TriggerScope down(trace::Trigger::PointerDown);
        trace::WriterScope write("outerWriter");
        trace::QueryScope query(outer);
        const auto outerOrigin = trace::origin;
        require(outer.queryID != 0 && sameOrigin(outer.origin, outerOrigin),
                "query failed to snapshot caller context");
        require(outer.origin.owner == reinterpret_cast<std::uintptr_t>(&outerOwner),
                "query attributed the wrong owner");
        {
            trace::TriggerScope move(trace::Trigger::PointerMove);
            require(sameOrigin(trace::origin, outerOrigin),
                    "nested mouse move hid its originating pointer-down event");
        }
        try {
            trace::OriginScope source2(trace::Source::BitmapColor, &innerOwner);
            trace::TriggerScope script(trace::Trigger::ScriptHitTest, false);
            trace::WriterScope write2("innerWriter");
            trace::QueryScope query2(inner);
            require(inner.queryID > outer.queryID && backendQuery() == &inner,
                    "nested backend read did not see its own query");
            require(inner.origin.source == trace::Source::BitmapColor &&
                    inner.origin.owner == reinterpret_cast<std::uintptr_t>(&innerOwner) &&
                    inner.origin.trigger == trace::Trigger::ScriptHitTest &&
                    inner.origin.parentTrigger == trace::Trigger::PointerDown,
                    "forced nested trigger lost its source or parent trigger");
            require(std::string(backendWriter()) == "innerWriter",
                    "nested writer context did not reach backend");
            backendComplete();
            require(inner.reported && !outer.reported,
                    "nested backend completion modified the parent query");
            throw std::runtime_error("deliberate unwind");
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()) == "deliberate unwind", error.what());
        }
        require(sameOrigin(trace::origin, outerOrigin) && backendQuery() == &outer &&
                std::string(backendWriter()) == "outerWriter",
                "exception unwind did not restore caller, trigger, query and writer");
        require(sameOrigin(outer.origin, outerOrigin),
                "nested caller changed the outer query snapshot");
        backendComplete();
        require(outer.reported && outer.lastSubmittedID == 71 && outer.renderFrame == 92 &&
                outer.wallNS == 19000000 && outer.gpuWaitNS == 17000000 &&
                outer.finishedNS == 123456789,
                "backend timing/submission metadata did not reach caller");
        require(outer.textureID != 0 && outer.version == 12 && outer.x == 17 && outer.y == 23 &&
                outer.width == 1920 && outer.height == 1080 && outer.alphaOnly &&
                std::string(outer.missReason) == "invalidated" &&
                outer.invalidation == trace::Invalidation::GPUOverwrite &&
                std::string(outer.writer) == "emote.draw" && outer.invalidatedVersion == 11,
                "backend completion overwrote query or invalidation metadata");
    }
    requireEmpty();
    {
        // A window event is active before its script callback or layer-manager
        // dispatch. An explicit script query retains that event as its parent.
        trace::TriggerScope window(trace::Trigger::PointerUp, false);
        {
            trace::OriginScope callback(trace::Source::LayerMask, &outerOwner);
            trace::Query callbackQuery;
            trace::QueryScope read(callbackQuery);
            require(callbackQuery.origin.trigger == trace::Trigger::PointerUp,
                    "window script callback lost the input event context");
        }
        {
            trace::TriggerScope script(trace::Trigger::ScriptHitTest, false);
            trace::OriginScope layer(trace::Source::LayerHitTest, &outerOwner);
            trace::Query scriptQuery;
            trace::QueryScope read(scriptQuery);
            require(scriptQuery.origin.trigger == trace::Trigger::ScriptHitTest &&
                    scriptQuery.origin.parentTrigger == trace::Trigger::PointerUp,
                    "script hit-test query lost its input-event parent");
        }
        {
            trace::TriggerScope recheck(trace::Trigger::InputRecheck, false);
            trace::TriggerScope forceRecheck(trace::Trigger::InputRecheck);
            trace::TriggerScope move(trace::Trigger::PointerMove);
            require(trace::origin.trigger == trace::Trigger::InputRecheck &&
                    trace::origin.parentTrigger == trace::Trigger::PointerUp,
                    "synthetic recheck was mislabeled or lost its parent event");
        }
        require(trace::origin.trigger == trace::Trigger::PointerUp &&
                trace::origin.parentTrigger == trace::Trigger::Unknown,
                "nested script/recheck attribution leaked into input handler");
    }
    requireEmpty();
}

static void enableChangesDoNotLeakScopes() {
    trace::SetEnabled(true);
    int owner = 0;
    trace::Query active, disabled;
    {
        trace::OriginScope source(trace::Source::LayerMask, &owner);
        trace::WriterScope writer("activeWriter");
        trace::QueryScope query(active);
        const auto caller = trace::origin;
        trace::SetEnabled(false);
        require(backendQuery() == nullptr && backendWriter() == nullptr,
                "disabled backend still exposes active contexts");
        {
            trace::OriginScope source2(trace::Source::BitmapMask, nullptr);
            trace::TriggerScope trigger(trace::Trigger::Click, false);
            trace::WriterScope writer2("disabledWriter");
            trace::QueryScope query2(disabled);
            require(disabled.queryID == 0 && sameOrigin(trace::origin, caller),
                    "disabled nested scopes modified a live diagnostic context");
        }
    }
    trace::SetEnabled(true);
    requireEmpty();
    trace::Query next;
    {
        trace::QueryScope scope(next);
        require(next.queryID > active.queryID, "query IDs were reused after re-enable");
    }
    requireEmpty();
}

static void threadIsolationAndTextureIDs() {
    constexpr std::size_t workers = 4, perWorker = 128;
    std::array<std::array<std::uint64_t, perWorker>, workers> textureIDs{};
    std::array<std::uint64_t, workers> queryIDs{};
    std::atomic<std::size_t> entered{0};
    std::mutex errorMutex;
    std::exception_ptr threadError;
    std::vector<std::thread> threads;
    int mainOwner = 0;
    trace::Query mainQuery;
    const auto stableTexture = trace::NextTextureID();
    mainQuery.textureID = stableTexture;
    {
        trace::OriginScope source(trace::Source::LayerColor, &mainOwner);
        trace::TriggerScope trigger(trace::Trigger::Wheel);
        trace::WriterScope writer("mainWriter");
        trace::QueryScope query(mainQuery);
        for (std::size_t index = 0; index < workers; ++index) {
            threads.emplace_back([&, index] {
                try {
                    const bool startedEmpty = sameOrigin(trace::origin, {}) &&
                        trace::query == nullptr && trace::writer == nullptr;
                    int owner = 0;
                    trace::Query local;
                    {
                        trace::OriginScope origin(trace::Source::BitmapMask, &owner);
                        trace::TriggerScope trigger(trace::Trigger::Click);
                        trace::WriterScope writer("workerWriter");
                        trace::QueryScope query(local);
                        entered.fetch_add(1, std::memory_order_release);
                        while (entered.load(std::memory_order_acquire) < workers)
                            std::this_thread::yield();
                        require(startedEmpty, "worker inherited caller TLS context");
                        require(backendQuery() == &local &&
                                trace::origin.owner == reinterpret_cast<std::uintptr_t>(&owner) &&
                                trace::origin.source == trace::Source::BitmapMask &&
                                trace::origin.trigger == trace::Trigger::Click &&
                                std::string(backendWriter()) == "workerWriter",
                                "concurrent caller context leaked across threads");
                        queryIDs[index] = local.queryID;
                        for (auto& id : textureIDs[index]) id = trace::NextTextureID();
                        backendComplete();
                        require(local.reported, "worker backend lost its query");
                    }
                    requireEmpty();
                } catch (...) {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    if (!threadError) threadError = std::current_exception();
                }
            });
        }
        for (auto& thread : threads) thread.join();
        if (threadError) std::rethrow_exception(threadError);
        require(backendQuery() == &mainQuery && !mainQuery.reported &&
                trace::origin.owner == reinterpret_cast<std::uintptr_t>(&mainOwner) &&
                trace::origin.trigger == trace::Trigger::Wheel &&
                std::string(backendWriter()) == "mainWriter",
                "worker reads changed the main thread context");
        require(mainQuery.textureID == stableTexture, "query changed texture identity");
    }
    requireEmpty();
    std::vector<std::uint64_t> allTextures{stableTexture};
    for (const auto& values : textureIDs)
        allTextures.insert(allTextures.end(), values.begin(), values.end());
    std::sort(allTextures.begin(), allTextures.end());
    require(allTextures.front() != 0 &&
            std::adjacent_find(allTextures.begin(), allTextures.end()) == allTextures.end(),
            "concurrent texture IDs collided or used the unknown sentinel");
    std::sort(queryIDs.begin(), queryIDs.end());
    require(queryIDs.front() > mainQuery.queryID &&
            std::adjacent_find(queryIDs.begin(), queryIDs.end()) == queryIDs.end(),
            "concurrent query IDs collided");
    trace::SetEnabled(false);
    // Texture identities outlive a diagnostic window; unlike query IDs,
    // explicit texture-ID allocation remains valid while tracing is disabled.
    const auto disabledTexture = trace::NextTextureID();
    trace::SetEnabled(true);
    require(disabledTexture > allTextures.back() && trace::NextTextureID() > disabledTexture,
            "texture identity was reset when diagnostic capture changed");
}

int main() {
    static_assert(!std::is_copy_constructible<trace::OriginScope>::value);
    static_assert(!std::is_copy_constructible<trace::TriggerScope>::value);
    static_assert(!std::is_copy_constructible<trace::QueryScope>::value);
    static_assert(!std::is_copy_constructible<trace::WriterScope>::value);
    try {
        disabledScopes();
        nestedScopesAndHandoff();
        enableChangesDoNotLeakScopes();
        threadIsolationAndTextureIDs();
        requireEmpty();
        std::cout << "PASS disabled, nested/unwind, backend handoff, TLS, unique IDs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        parser.error("a C++17 compiler is required (--cxx or CXX)")
    include = ROOT / "Engine/KRKRRuntime/Source/cpp/core/render"
    with tempfile.TemporaryDirectory(prefix="mikage-point-read-trace-") as directory:
        directory = Path(directory)
        test = directory / "point_trace_test.cpp"
        backend = directory / "point_trace_backend.cpp"
        test.write_text(TESTS, encoding="utf-8")
        backend.write_text(BACKEND, encoding="utf-8")
        for name, flags in (("debug", ["-O0", "-g"]), ("release", ["-O2", "-DNDEBUG"])):
            executable = directory / ("point_trace_" + name + (".exe" if os.name == "nt" else ""))
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                            *flags, "-I", str(include), str(test), str(backend),
                            "-o", str(executable)], check=True, timeout=90)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
            print(f"PointReadTrace ({name}): {result.stdout.strip() or result.stderr.strip()}", flush=True)
            if result.returncode:
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
