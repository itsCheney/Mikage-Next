// Minimal platform services only. All render methods, sampling, tables and
// texture implementations come from production RenderManager.cpp.
#include "tjsCommHead.h"
#include "LayerBitmap.h"
#include "TVPSystem.h"
#include "TVPEvent.h"
#include "PlatformThread.h"
#include "TVPMsg.h"
#include <chrono>
#include <stdexcept>
void TVPConsoleLog(const tjs_char*,...) {}
tjs_uint64 TVPGetRoughTickCount() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
int TVPGetThreadNum() { return 1; }
void TVPExecThreadTask(int n,TVP_THREAD_TASK_FUNC f) { for(int i=0;i<n;++i) f(i); }
void TVPAddAtExitHandler(tjs_int,void (*)()) {}
void TVPAddContinuousEventHook(tTVPContinuousEventCallbackIntf*) {}
void TVPRemoveContinuousEventHook(tTVPContinuousEventCallbackIntf*) {}
void TVPCheckMemory() {}
int TVPShowSimpleMessageBox(const ttstr&,const ttstr&) { return 0; }
void TVPThrowExceptionMessage(const tjs_char* message) { throw std::runtime_error(message); }
void TVPThrowExceptionMessage(const tjs_char* message,const ttstr&) { throw std::runtime_error(message); }
void TVPThrowExceptionMessage(const tjs_char* message,const ttstr&,const ttstr&) { throw std::runtime_error(message); }
tTJSMessageHolder TVPOutOfRectangle(TJS_N("OutOfRectangle"),TJS_N("out of rectangle"));
tTJSMessageHolder TVPCannotAllocateBitmapBits(TJS_N("CannotAllocateBitmapBits"),TJS_N("allocation failed"));
tTVPBitmap::tTVPBitmap(tjs_uint w,tjs_uint h,tjs_uint bpp) : RefCount(1), Bits(nullptr), BitmapInfo(nullptr), Palette(nullptr) {
    Width=w; Height=h; BitmapInfo=new BitmapInfomation(w,h,bpp);
    PitchBytes=PitchStep=BitmapInfo->GetPitchBytes(); Bits=std::calloc(h,PitchBytes);
}
tTVPBitmap::~tTVPBitmap() { std::free(Bits); delete BitmapInfo; }
void* tTVPBitmap::GetScanLine(tjs_uint y) const { return static_cast<uint8_t*>(Bits)+size_t(y)*PitchStep; }

#include "TVPCompositor.h"
namespace krkrsdl3 { void TVPRegisterRenderBackend(const TVPRenderBackendDesc&) {} }
