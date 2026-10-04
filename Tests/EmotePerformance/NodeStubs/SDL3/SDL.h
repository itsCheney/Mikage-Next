#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
using Uint64 = std::uint64_t;
inline std::map<std::string, std::string>& NodeTestHints() { static std::map<std::string, std::string> hints; return hints; }
inline const char* SDL_GetHint(const char* key) {
    const auto value = NodeTestHints().find(key);
    return value == NodeTestHints().end() ? nullptr : value->second.c_str();
}
inline bool SDL_SetHint(const char* key, const char* value) { NodeTestHints()[key] = value; return true; }
inline bool SDL_GetHintBoolean(const char* key, bool fallback) {
    const char* value = SDL_GetHint(key);
    return value ? std::string(value) == "1" || std::string(value) == "true" : fallback;
}
inline Uint64 SDL_GetTicksNS() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline Uint64 SDL_GetTicks() { return SDL_GetTicksNS() / 1000000; }
