#!/usr/bin/env python3
"""Exercise production KAG cache code across embedded game sessions.

This compiles the cache section from each prepared engine source, along with
the actual compact-hook reset and static-cleanup registry functions. The small
test double supplies scenario contents and a reference-counted cache; this is a
lifecycle regression test, not a full TJS interpreter or iOS rendering test.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
PARSERS = {
    "KAGParser": "core/script/tjsNativeKAGParser.cpp",
    "KAGParserEx": "plugins/KAGParserEx/KAGParserEx.cpp",
    "ExtKAGParser": "plugins/ExtKAGParser/ExtKAGParser.cpp",
}


def function(text, signature):
    """Extract these known functions, which have no braces in string literals."""
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
    parser.add_argument("--source", required=True, type=Path,
                        help="KRKR cpp directory from the pinned Mikage core fork")
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        parser.error("a C++17 compiler is required (--cxx or CXX)")

    events = (args.source / "core/main/TVPEvent.cpp").read_text(encoding="utf-8")
    native = (args.source / "tjs2/tjsNative.cpp").read_text(encoding="utf-8")
    production_lifecycle = "\n".join([
        function(events, "void TVPAddCompactEventHook("),
        function(events, "void TVPResetEventState()"),
        function(native, "void TJSAddStaticToRegisterHeap("),
        function(native, "void TJSClearRegisterHeap()"),
    ])
    fixture = ROOT / "Tests/KRKRRuntime"
    prefix = (fixture / "ScenarioCacheHarness.hpp").read_text(encoding="utf-8")
    tests = (fixture / "ScenarioCacheHarness.cpp").read_text(encoding="utf-8")
    failures = 0
    with tempfile.TemporaryDirectory(prefix="mikage-scenario-cache-") as temp:
        temp = Path(temp)
        for name, relative in PARSERS.items():
            text = (args.source / relative).read_text(encoding="utf-8")
            start = text.index("#define TVP_SCENARIO_MAX_CACHE_SIZE")
            get_scenario = function(text, "tTVPScenarioCacheItem* TVPGetScenario(" if
                                    name == "KAGParser" else
                                    "static tTVPScenarioCacheItemEX* TVPGetScenario(" if
                                    name == "KAGParserEx" else
                                    "static tExtTVPScenarioCacheItem* TVPGetScenario(")
            end = text.index(get_scenario) + len(get_scenario)
            unit = temp / (name + ".cpp")
            unit.write_text(prefix + "\n" + production_lifecycle + "\n" +
                            text[start:end] + "\n" + tests, encoding="utf-8")
            for debug in (False, True):
                mode = "debug" if debug else "release"
                exe = temp / (name + "-" + mode + (".exe" if os.name == "nt" else ""))
                command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                           str(unit), "-o", str(exe)]
                if debug:
                    command.append("-D_DEBUG")
                subprocess.run(command, check=True)
                result = subprocess.run([str(exe)], capture_output=True, text=True)
                print(f"{name} ({mode}): {result.stdout.strip() or result.stderr.strip()}",
                      flush=True)
                failures += result.returncode != 0
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
