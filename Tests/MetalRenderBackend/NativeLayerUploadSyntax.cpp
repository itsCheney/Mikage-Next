// Compile the actual native C3 test bodies on portable hosts too. These are
// declarations only: this object is never linked or presented as a GPU test.
#include "backend/MetalRenderBackend.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <chrono>
#include <iostream>
bool SDL_ShowWindow(SDL_Window*);
bool SDL_HideWindow(SDL_Window*);
void SDL_PumpEvents();
using krkrsdl3::iTVPRenderBackend;
bool SDL_SetHint(const char*,const char*);
bool SDL_GetHintBoolean(const char*,bool);
void SDL_Delay(unsigned);
void Require(bool,const char*);
void Compare(const std::vector<uint8_t>&,const std::vector<uint8_t>&,int);
namespace krkrsdl3 {
extern int g_blitEncoders,g_renderEncoders,g_metalSubmits,g_syncWaits;
}
#include "NativeLayerUploadTests.inc"
#include "NativeLayerInitializationTests.inc"

#include "NativePendingFillTests.inc"

#include "NativeStaticPresentationTests.inc"
