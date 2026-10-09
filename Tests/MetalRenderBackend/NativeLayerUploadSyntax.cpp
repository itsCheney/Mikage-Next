// Compile the actual native C3 test bodies on portable hosts too. These are
// declarations only: this object is never linked or presented as a GPU test.
#include "backend/MetalRenderBackend.h"
#include <cstring>
#include <vector>
using krkrsdl3::iTVPRenderBackend;
bool SDL_SetHint(const char*,const char*);
void Require(bool,const char*);
void Compare(const std::vector<uint8_t>&,const std::vector<uint8_t>&,int);
namespace krkrsdl3 {
extern int g_blitEncoders,g_renderEncoders,g_metalSubmits,g_syncWaits;
}
#include "NativeLayerUploadTests.inc"
