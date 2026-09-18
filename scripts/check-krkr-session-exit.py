#!/usr/bin/env python3
"""Run production exit/close/archive methods with instrumented dependencies."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "Engine/KRKRRuntime/Source/cpp"


def function(path, signature):
    source = path.read_text(encoding="utf-8")
    start = source.index(signature)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        parser.error("a C++17 compiler is required")
    prefix = r'''
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
bool systemAlive, pluginsAlive, vmAlive, backendAlive;
int saves, stoppedLoaders, nullHolders, cacheAdds;
const int TVP_COMPACT_LEVEL_MAX=2;
bool TVPProjectDirSelected=true, TVPSystemControlAlive=true;
struct Loader { ~Loader() { assert(systemAlive && pluginsAlive); ++stoppedLoaders; } };
struct Control {};
Control* TVPSystemControl=nullptr;
struct tTVPApplication { Loader* image_load_thread_=nullptr; void OnExit(); };
void TVPInvalidateMovieSession() {} void TVPFinalizeVideoOverlaySession() {}
void* TVPGetScriptEngine() { return vmAlive ? reinterpret_cast<void*>(1) : nullptr; }
void TVPDeliverCompactEvent(int) {
    // The game's save callback loads another script from its ZIP archive.
    assert(systemAlive && pluginsAlive && vmAlive && backendAlive); ++saves;
}
void TVPSystemUninit() { systemAlive=false; }
void TVPUnloadPlugins() { pluginsAlive=false; }
void TVPResetEventState() {} void TVPResetGraphicSessionState() {}
void TVPUninitScriptEngine() { vmAlive=false; }
void TVPClearAllAutoPath() {} void TVPClearAllWindows() {}
namespace krkrsdl3 { void TVPClearAllTexture() { assert(backendAlive); } }
struct iTVPTexture2D { static void RecycleProcess() { assert(backendAlive); } };
struct TVPWindow {
    bool Closing=false, ProgramClosing=false;
    int queries=0, forced=0;
    bool OnCloseQuery() { assert(!ProgramClosing); ++queries; Closing=true; return false; }
    void Close() { ++forced; }
    void RequestUserClose();
};
using ttstr=std::string; using tjs_uint32=unsigned; using tjs_int=int;
struct tTVPArchive {};
struct tHolder {
    tTVPArchive* object;
    explicit tHolder(tTVPArchive* p):object(p) { if(!p) ++nullHolders; }
    tTVPArchive* GetObject() { return object; }
};
template<class K,class V> struct tTJSHashCache {
    static unsigned MakeHash(const K&) { return 0; }
    V* FindAndTouchWithHash(const K&,unsigned) { return nullptr; }
    void AddWithHash(const K&,unsigned,const V&) { ++cacheAdds; }
};
struct tTJSCSH { explicit tTJSCSH(int) {} };
ttstr TVPNormalizeStorageName(ttstr name) { return name; }
bool TVPIsExistentStorageNoSearch(const ttstr&) { return true; }
tTVPArchive* TVPOpenArchive(const ttstr&,bool) { return nullptr; }
#define TJS_N(s) s
const char* TVPCannotFindStorage="missing";
void TVPThrowExceptionMessage(const char* message,const ttstr&) { throw std::runtime_error(message); }
struct tTVPArchiveCache {
    tTJSHashCache<ttstr,tHolder> ArchiveCache; int CS=0;
    tTVPArchive* Get(ttstr name);
};
'''
    production = function(CORE / "core/main/TVPApplication.cpp", "void tTVPApplication::OnExit()")
    production += "\n" + function(CORE / "core/main/TVPWindow.cpp", "void TVPWindow::RequestUserClose()")
    archive = function(CORE / "core/archive/TVPStorage.cpp", "    tTVPArchive* Get(ttstr name)")
    production += "\n" + archive.replace("tTVPArchive* Get(", "tTVPArchive* tTVPArchiveCache::Get(", 1)
    tests = r'''
int main() {
    for(int session=0;session<50;++session) {
        systemAlive=pluginsAlive=vmAlive=backendAlive=true;
        TVPSystemControl=new Control;
        tTVPApplication application;application.image_load_thread_=new Loader;
        application.OnExit();assert(!vmAlive && !systemAlive && !pluginsAlive);
        assert(application.image_load_thread_==nullptr && backendAlive);
    }
    assert(saves==50 && stoppedLoaders==50);
    systemAlive=pluginsAlive=backendAlive=true;vmAlive=false;
    tTVPApplication earlyFailure;earlyFailure.OnExit();assert(saves==50);
    TVPWindow window;window.RequestUserClose();assert(window.queries==1 && window.forced==0);
    window.RequestUserClose();assert(window.queries==1);
    window.Closing=false;window.RequestUserClose();assert(window.queries==2 && window.forced==0);
    tTVPArchiveCache cache;bool rejected=false;
    try { cache.Get("game.zip"); } catch(const std::runtime_error&) { rejected=true; }
    assert(rejected && nullHolders==0 && cacheAdds==0);
    std::cout<<"PASS: 50 save-before-storage/plugin-shutdown sessions, early failure, native close cancellation and null archive guard\n";
}
'''
    with tempfile.TemporaryDirectory(prefix="mikage-session-exit-") as directory:
        unit = Path(directory) / "session-exit.cpp"
        exe = Path(directory) / ("session-exit.exe" if os.name == "nt" else "session-exit")
        unit.write_text(prefix + production + tests, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(unit), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
