#!/usr/bin/env python3
"""Exercise the production host's interval/work-duration accumulator."""
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
    tests = r'''
int main() {
    resetStats();
    Uint64 now = fakeNow;
    for (int i = 0; i < 60; ++i) {
        now += 16666667;
        recordFrame(now, 2000000);
    }
    assert(std::abs(currentFPS - 60.0) < 0.001);
    assert(std::abs(currentFrameTimeMS - 16.666667) < 0.001);
    assert(std::abs(currentCpuFrameTimeMS - 2.0) < 0.001);
    assert(std::abs(currentMaxCpuFrameTimeMS - 2.0) < 0.001);
    // A scene-load spike must appear in generation peak, independently of
    // the display cadence and the rolling average frame interval.
    for (int i = 0; i < 30; ++i) {
        Uint64 work = i == 0 ? 120000000 : 2000000;
        now += i == 0 ? work : 33333334;
        recordFrame(now, work);
    }
    assert(currentFPS > 20 && currentFPS < 30);
    assert(currentCpuFrameTimeMS > 2 && currentCpuFrameTimeMS < 10);
    assert(std::abs(currentMaxCpuFrameTimeMS - 120.0) < 0.001);
    fakeNow = now + 10000000000ULL;
    resetStats();
    assert(currentFPS == 0 && currentFrameTimeMS == 0);
    assert(currentCpuFrameTimeMS == 0 && currentMaxCpuFrameTimeMS == 0);
    assert(frameCount == 0 && frameWorkTotal == 0 && frameWorkMax == 0);
    std::cout << "PASS: cadence/work separation, scene spike peak and reset\n";
}
'''
    prefix = """#include <cstdint>
#include <cassert>
#include <cmath>
#include <iostream>
using Uint64 = std::uint64_t;
Uint64 fakeNow = 1000000000ULL;
Uint64 SDL_GetTicksNS() { return fakeNow; }
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
