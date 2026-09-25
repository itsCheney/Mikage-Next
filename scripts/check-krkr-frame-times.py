#!/usr/bin/env python3
"""Exercise frame timing, peak-stage diagnostics and bounded Emote stall logs."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    args = parser.parse_args()
    cxx = args.cxx or shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        parser.error("a C++17 compiler is required")
    host = (ROOT / "Engine/KRKRRuntime/Source/host/MikageKRKRRuntime.mm").read_text(encoding="utf-8")
    globals_ = host[host.index("Uint64 statsWindowStarted ="):host.index("void resetStats()")]
    production = globals_ + function(host, "void resetStats()") + "\n" + function(host, "void recordFrame(")
    emote = (ROOT / "Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/emoteplayerclass.cpp").read_text(encoding="utf-8")
    emote_header = (ROOT / "Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/emoteplayerclass.h").read_text(encoding="utf-8")
    shared_header = (ROOT / "Engine/KRKRRuntime/Source/cpp/plugins/emoteplayer/emoteresourcecache.h").read_text(encoding="utf-8")
    production += "\n" + function(emote_header, "struct EmoteResourceDiagnostics") + ";\n"
    production += function(shared_header, "struct EmoteSharedResourceCacheStats") + ";\n"
    production += "EmoteSharedResourceCacheStats sharedStats;\n"
    production += "EmoteSharedResourceCacheStats GetSharedEmoteResourceCacheStats() { return sharedStats; }\n"
    production += function(emote, "static void recordSlowEmoteOperation(")
    tests = r'''
int main() {
    resetStats();
    Uint64 now = fakeNow;
    for (int i = 0; i < 60; ++i) {
        now += 16666667;
        recordFrame(now, 2000000, 100000, 1800000, 1);
    }
    assert(std::abs(currentFPS - 60.0) < 0.001);
    assert(std::abs(currentFrameTimeMS - 16.666667) < 0.001);
    assert(std::abs(currentCpuFrameTimeMS - 2.0) < 0.001);
    assert(std::abs(currentMaxCpuFrameTimeMS - 2.0) < 0.001);
    assert(logs.empty());
    // A scene-load spike must appear in generation peak, independently of
    // the display cadence and the rolling average frame interval.
    for (int i = 0; i < 30; ++i) {
        Uint64 work = i == 0 ? 120000000 : 2000000;
        now += i == 0 ? work : 33333334;
        recordFrame(now, work, i == 0 ? 110000000 : 100000, i == 0 ? 9000000 : 1800000, i == 0 ? 7 : 1);
    }
    assert(currentFPS > 20 && currentFPS < 30);
    assert(currentCpuFrameTimeMS > 2 && currentCpuFrameTimeMS < 10);
    assert(std::abs(currentMaxCpuFrameTimeMS - 120.0) < 0.001);
    assert(logs.size() == 1);
    assert(logs.back().find("count=1 peakWallMS=120.000 peakEventMS=110.000 peakIterateMS=9.000 peakEvents=7") != std::string::npos);
    // Continuous slow frames generate one summary per second, and peak fields
    // remain tied to one frame rather than combining unrelated stage maxima.
    fakeNow = now;
    resetStats();
    logs.clear();
    for (int i = 0; i < 20; ++i) {
        now += 60000000;
        recordFrame(now, i == 0 ? 60000000 : 50000000,
                    i == 0 ? 1000000 : 49000000, i == 0 ? 59000000 : 1000000, 2);
    }
    assert(logs.size() == 1);
    assert(logs.back().find("count=17 peakWallMS=60.000 peakEventMS=1.000 peakIterateMS=59.000") != std::string::npos);
    fakeNow = now + 10000000000ULL;
    resetStats();
    assert(currentFPS == 0 && currentFrameTimeMS == 0);
    assert(currentCpuFrameTimeMS == 0 && currentMaxCpuFrameTimeMS == 0);
    assert(frameCount == 0 && frameWorkTotal == 0 && frameWorkMax == 0);
    assert(slowFrameCount == 0 && peakFrameEventTimeNS == 0 && peakFrameIterateTimeNS == 0);
    assert(peakFrameEventCount == 0);

    logs.clear();
    recordSlowEmoteOperation(true, fakeNow - 49000000);
    assert(logs.empty());
    recordSlowEmoteOperation(true, fakeNow - 100000000, 0, 95000000, true);
    assert(logs.size() == 1);
    assert(logs.back().find("resourceLoad wallMS=100.000 fileLoadMS=0.000 rootMS=95.000 cacheHit=1") != std::string::npos);
    fakeNow += 200000000;
    recordSlowEmoteOperation(true, fakeNow - 150000000);
    recordSlowEmoteOperation(true, fakeNow - 50000000);
    assert(logs.size() == 1);
    // Play and resource-load reports have independent limits.
    recordSlowEmoteOperation(false, fakeNow - 70000000);
    assert(logs.size() == 2 && logs.back().find("operation=play") != std::string::npos);
    fakeNow += 1000000000;
    recordSlowEmoteOperation(true, fakeNow - 70000000, 60000000, 5000000);
    assert(logs.size() == 3);
    assert(logs.back().find("suppressed=2 suppressedPeakWallMS=150.000") != std::string::npos);
    fakeNow += 1000000000;
    recordSlowEmoteOperation(true, fakeNow - 50000000);
    assert(logs.back().find("suppressed=0 suppressedPeakWallMS=0.000") != std::string::npos);
    fakeNow += 1000000000;
    EmoteResourceDiagnostics diagnostic{7, 5, 2, 3, 0, 1, 0, 0};
    recordSlowEmoteOperation(true, fakeNow - 70000000, 60000000, 0, false,
                             &diagnostic, 0x1234ULL, 2, false);
    assert(logs.back().find("rootMS=0.000 cacheHit=0") != std::string::npos);
    assert(logs.back().find("rootRequested=0 managerId=7 resourceId=0000000000001234 entries=2") != std::string::npos);
    assert(logs.back().find("loads=5 cacheHits=2 cacheMisses=3 failures=0 unloads=1") != std::string::npos);
    sharedStats = {11, 13, 17, 4096, 2, 0};
    fakeNow += 1000000000;
    recordSlowEmoteOperation(true, fakeNow - 70000000, 60000000, 0, false,
                             &diagnostic, 0x1234ULL, 2, false, true);
    assert(logs.back().find("sharedCacheHit=1 customDecrypt=0 archiveFilter=0 sharedHits=11 sharedMisses=13") != std::string::npos);
    assert(logs.back().find("sharedBytes=4096 sharedEntries=2 sharedEvictions=17") != std::string::npos);
    fakeNow += 1000000000;
    recordSlowEmoteOperation(true, fakeNow - 70000000, 0, 60000000, true,
                             &diagnostic, 0x1234ULL, 2, true, true, true);
    assert(logs.back().find("sharedCacheHit=0 customDecrypt=0") != std::string::npos);
    fakeNow += 1000000000;
    recordSlowEmoteOperation(true, fakeNow - 70000000, 60000000, 0, false,
                             &diagnostic, 0x1234ULL, 2, false, false, false, true);
    assert(logs.back().find("archiveFilter=1") != std::string::npos);
    // Even maximum-width counters must fit the runtime's conservative 1 KiB
    // test log buffer. TVPConsoleLog below asserts rather than truncates.
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    diagnostic = {maximum, maximum, maximum, maximum, maximum, maximum, maximum, maximum};
    sharedStats = {maximum, maximum, maximum, static_cast<std::size_t>(maximum),
                   static_cast<std::size_t>(maximum), maximum};
    fakeNow += 1000000000;
    recordSlowEmoteOperation(true, fakeNow - 70000000, 60000000, 0, false,
                             &diagnostic, maximum, static_cast<std::size_t>(maximum), false, true);
    std::cout << "PASS: cadence/work separation, peak-stage correlation, reset and bounded Emote stall logs\n";
}
'''
    prefix = """#include <cstdint>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
using Uint64 = std::uint64_t;
Uint64 fakeNow = 1000000000ULL;
Uint64 SDL_GetTicksNS() { return fakeNow; }
std::vector<std::string> logs;
void MikageKRKRLogMessage(const char *, int, const char *message) { logs.emplace_back(message); }
void TVPConsoleLog(const char *format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    assert(written >= 0 && static_cast<std::size_t>(written) < sizeof(message));
    logs.emplace_back(message);
}
"""
    with tempfile.TemporaryDirectory(prefix="mikage-frame-times-") as directory:
        unit = Path(directory) / "frame-times.cpp"
        exe = Path(directory) / ("frame-times.exe" if os.name == "nt" else "frame-times")
        unit.write_text(prefix + production + tests, encoding="utf-8")
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(unit), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
