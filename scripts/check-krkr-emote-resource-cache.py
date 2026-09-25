#!/usr/bin/env python3
"""Exercise Emote cache ownership and native/script load contracts with fake files."""
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
    base = ROOT / "Engine/KRKRRuntime/Source/cpp"
    source = (base / "plugins/emoteplayer/emoteplayerclass.cpp").read_text(encoding="utf-8")
    header = (base / "plugins/emoteplayer/emoteplayerclass.h").read_text(encoding="utf-8")
    native = (base / "plugins/DrawDeviceD3D/D3DEmotePlayer.cpp").read_text(encoding="utf-8")
    native_load = function(native, "tjs_error D3DEmotePlayer::load(")
    assert "RM->ensureLoaded(tmpname)" in native_load and "RM->load(tmpname)" not in native_load
    assert "_motionWorkLayer == nullptr && kagWindow != nullptr" in function(source, "ResourceManager::ResourceManager(")
    declarations = function(header, "struct EmoteResourceDiagnostics") + ";\n"
    declarations += function(header, "class ResourceManager\n") + ";\n"
    production = "\n".join(function(source, signature) for signature in [
        "static std::uint64_t nextEmoteManagerId(",
        "static std::uint64_t emoteResourceId(",
        "static void recordEmoteCacheEvent(",
        "static void recordSlowEmoteOperation(",
        "ResourceManager::~ResourceManager(",
        "tTJSVariant ResourceManager::load(",
        "void ResourceManager::ensureLoaded(",
        "tTJSVariant ResourceManager::loadInternal(",
        "void ResourceManager::unload(",
        "void ResourceManager::unloadAll(",
        "void ResourceManager::unloadAllInternal(",
        "void ResourceManager::clearCache(",
        "emotefile* ResourceManager::GetPlayerByName(",
        "void ResourceManager::setEmotePSBDecryptSeed(",
        "void ResourceManager::setEmotePSBDecryptFunc(",
    ])
    prefix = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "md5.h"
#include "emoteresourcecache.h"
using emoteplayer::EmoteSharedResourceCacheStats;
using Uint64 = std::uint64_t;
using tjs_int = int;
using tjs_char = wchar_t;
struct tTJSVariantClosure {
    void* Object = nullptr;
    tTJSVariantClosure(std::nullptr_t = nullptr) {}
};
class iTJSDispatch2 {};
#define TJS_N(value) L##value
class ttstr : public std::wstring {
public:
    using std::wstring::wstring;
    ttstr(const std::wstring& value) : std::wstring(value) {}
    bool StartsWith(const wchar_t* value) const { return find(value) == 0; }
    ttstr SubString(std::size_t from, std::size_t size) const { return substr(from, size); }
};
using tTJSString = ttstr;
struct tTJSVariant {
    std::shared_ptr<int> object;
    tTJSVariantClosure closure;
    tTJSVariantClosure AsObjectClosure() const { return closure; }
};
Uint64 fakeNow = 1000000000ULL;
Uint64 SDL_GetTicksNS() { return fakeNow; }
std::vector<std::string> logs;
EmoteSharedResourceCacheStats sharedStats;
int sharedClears = 0;
EmoteSharedResourceCacheStats GetSharedEmoteResourceCacheStats() { return sharedStats; }
void ClearSharedEmoteResourceCache() {
    ++sharedClears;
    sharedStats.retainedBytes = sharedStats.entries = 0;
    ++sharedStats.generation;
}
void TVPConsoleLog(const char *format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    assert(written >= 0 && static_cast<std::size_t>(written) < sizeof(message));
    logs.emplace_back(message);
}
void TVPGetRandomBits128(void* output) {
    auto bytes = static_cast<unsigned char*>(output);
    for (int i = 0; i < 16; ++i) bytes[i] = static_cast<unsigned char>(17 * i + 3);
}
[[noreturn]] void TVPThrowExceptionMessage(const wchar_t*) { throw std::runtime_error("load failed"); }
int resolutions = 0;
ttstr TVPGetPlacedPath(const ttstr& input) {
    ++resolutions;
    return input.StartsWith(L"./") ? input.SubString(2, input.size() - 2) : input;
}
int liveFiles = 0, fileLoads = 0, rootReads = 0, loadMode = 0;
bool nextSharedHit = false;
class emotefile {
    bool sharedHit = false;
public:
    emotefile() { ++liveFiles; }
    ~emotefile() { --liveFiles; }
    void setSeed(int) {}
    void setFun(tTJSVariantClosure) {}
    bool load(const ttstr&) {
        ++fileLoads;
        fakeNow += 60000000;
        if (loadMode == 1) return false;
        if (loadMode == 2) throw std::runtime_error("decode failed");
        sharedHit = nextSharedHit;
        if (sharedHit) ++sharedStats.hits;
        else ++sharedStats.misses;
        return true;
    }
    bool WasSharedCacheHit() const { return sharedHit; }
    bool BypassedSharedCacheForArchiveFilter() const { return false; }
    tTJSVariant root() {
        ++rootReads;
        fakeNow += 80000000;
        return {std::make_shared<int>(rootReads), {}};
    }
};
'''
    tests = r'''
ResourceManager::ResourceManager(iTJSDispatch2*, tjs_int) {
    _diagnostics.managerId = nextEmoteManagerId();
}
int main() {
    const auto id = emoteResourceId(L"private/assets/hero.psb");
    assert(id == emoteResourceId(L"private/assets/hero.psb"));
    assert(id != emoteResourceId(L"private/assets/other.psb"));
    assert(nextEmoteManagerId() != nextEmoteManagerId());
    {
        ResourceManager manager(nullptr, 0);
        manager.ensureLoaded(L"lzfs://./private/assets/hero.psb");
        assert(fileLoads == 1 && rootReads == 0 && resolutions == 1 && liveFiles == 1);
        assert(logs.back().find("rootRequested=0") != std::string::npos);
        assert(logs.back().find("rootMS=0.000") != std::string::npos);
        manager.ensureLoaded(L"./private/assets/hero.psb");
        assert(fileLoads == 1 && rootReads == 0 && resolutions == 2);
        auto root1 = manager.load(L"private/assets/hero.psb");
        auto root2 = manager.load(L"private/assets/hero.psb");
        assert(fileLoads == 1 && rootReads == 2 && resolutions == 4);
        assert(root1.object != root2.object);
        *root1.object = 12345;
        assert(*root2.object != 12345);
        assert(manager.GetPlayerByName(L"private/assets/hero.psb") != nullptr);
        manager.clearCache();
        assert(liveFiles == 1); // Existing live-resource contract remains intact.
        assert(sharedClears == 1);
        fakeNow += 1000000000;
        manager.unload(L"lzfs://./private/assets/hero.psb");
        assert(liveFiles == 0 && resolutions == 5);
        manager.ensureLoaded(L"private/assets/hero.psb");
        assert(fileLoads == 2 && rootReads == 2 && liveFiles == 1);
        fakeNow += 1000000000;
        auto scriptRoot = manager.load(L"private/assets/hero.psb");
        assert(scriptRoot.object && rootReads == 3);
        assert(logs.back().find("rootRequested=1") != std::string::npos);
        assert(logs.back().find("cacheHits=4 cacheMisses=2 failures=0 unloads=1") != std::string::npos);
        for (int mode : {1, 2}) {
            loadMode = mode;
            bool threw = false;
            try { manager.ensureLoaded(L"broken.psb"); } catch (...) { threw = true; }
            assert(threw && liveFiles == 1);
            assert(manager.GetPlayerByName(L"broken.psb") == nullptr);
        }
        loadMode = 0;
        manager.ensureLoaded(L"broken.psb");
        assert(liveFiles == 2 && rootReads == 3);
        // Another manager loads its own mutable parsed file; diagnostics can
        // correlate resource fingerprints without globally sharing ownership.
        {
            ResourceManager other(nullptr, 0);
            other.ensureLoaded(L"private/assets/hero.psb");
            assert(liveFiles == 3);
            assert(other.GetPlayerByName(L"private/assets/hero.psb") !=
                   manager.GetPlayerByName(L"private/assets/hero.psb"));
        }
        assert(liveFiles == 2);
        manager.unloadAll();
        assert(liveFiles == 0);
    }
    assert(liveFiles == 0);
    {
        ResourceManager manager(nullptr, 0);
        nextSharedHit = true;
        fakeNow += 1000000000;
        manager.ensureLoaded(L"shared.psb");
        assert(logs.back().find("cacheHit=0") != std::string::npos);
        assert(logs.back().find("sharedCacheHit=1") != std::string::npos);
        const auto sharedHits = sharedStats.hits;
        fakeNow += 1000000000;
        manager.load(L"shared.psb"); // file remembers its prior hit, this call did not use shared cache
        assert(logs.back().find("cacheHit=1") != std::string::npos);
        assert(logs.back().find("sharedCacheHit=0") != std::string::npos);
        assert(sharedStats.hits == sharedHits);
        nextSharedHit = false;
        const int clearsBefore = sharedClears;
        ResourceManager::setEmotePSBDecryptSeed(123);
        assert(sharedClears == clearsBefore + 1 && liveFiles == 1);
        ResourceManager::setEmotePSBDecryptSeed(123);
        assert(sharedClears == clearsBefore + 1);
        tTJSVariant decrypt;
        decrypt.closure.Object = &manager;
        ResourceManager::setEmotePSBDecryptFunc(decrypt);
        assert(sharedClears == clearsBefore + 2 && liveFiles == 1);
        ResourceManager::setEmotePSBDecryptFunc(decrypt);
        assert(sharedClears == clearsBefore + 3); // reinstall may change script state
        fakeNow += 1000000000;
        manager.ensureLoaded(L"custom.psb");
        assert(logs.back().find("sharedCacheHit=0 customDecrypt=1") != std::string::npos);
        ResourceManager::setEmotePSBDecryptFunc({});
        ResourceManager::setEmotePSBDecryptSeed(0);
    }
    assert(liveFiles == 0);
    for (const auto& log : logs) {
        assert(log.find("private") == std::string::npos);
        assert(log.find("hero.psb") == std::string::npos);
        assert(log.find("broken.psb") == std::string::npos);
    }
    logs.clear();
    fakeNow += 1000000000;
    EmoteResourceDiagnostics diagnostic{99, 3, 1, 2, 0, 0, 0, 0};
    for (int i = 0; i < 100; ++i) recordEmoteCacheEvent("create", diagnostic, 0, id);
    assert(logs.size() == 1);
    fakeNow += 1000000000;
    recordEmoteCacheEvent("destroy", diagnostic, 0, id);
    assert(logs.size() == 2 && logs.back().find("suppressed=99") != std::string::npos);
    for (const auto& log : logs) {
        assert(log.find("private") == std::string::npos);
        assert(log.find("hero.psb") == std::string::npos);
    }
    std::cout << "PASS: native root bypass, fresh script trees, canonical cache keys, failed-load ownership, bounded opaque diagnostics\n";
}
'''
    with tempfile.TemporaryDirectory(prefix="mikage-emote-cache-") as directory:
        unit = Path(directory) / "emote-cache.cpp"
        exe = Path(directory) / ("emote-cache.exe" if os.name == "nt" else "emote-cache")
        unit.write_text(prefix + declarations + production + tests, encoding="utf-8")
        math = base / "core/utils/math"
        subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(math),
                        "-I", str(base / "plugins/emoteplayer"),
                        str(unit), "-x", "c++", str(math / "md5.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
