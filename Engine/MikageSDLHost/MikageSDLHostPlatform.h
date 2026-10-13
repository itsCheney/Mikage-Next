#pragma once

#include <string>

#include "MikageSDLHost.h"

// The platform audio session (AVAudioSession on iOS). Deactivating notifies
// other audio apps. Returns false and sets error when the system refuses.
MIKAGE_SDL_HOST_API bool MikageSDLHostPlatformSetAudioSessionActive(bool active, std::string &error);
