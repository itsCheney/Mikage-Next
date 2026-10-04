#include "MikageKRKRRuntime.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdio>
#include "emoteperformance.h"

enum SDL_HintPriority { SDL_HINT_DEFAULT, SDL_HINT_NORMAL, SDL_HINT_OVERRIDE };
static std::string hint, message;
static SDL_HintPriority lastPriority = SDL_HINT_DEFAULT;
static bool hintFails = false;
bool SDL_SetHintWithPriority(const char* name, const char* value, SDL_HintPriority priority) {
    if (hintFails) return false;
    SDL_SetHint(name, value);
    if (!std::strcmp(name, "MIKAGE_EMOTE_ANIMATION_MODE")) hint = value;
    lastPriority = priority; return true;
}
void MikageKRKRLogMessage(const char*, int32_t, const char* value) {
    if (std::strncmp(value,"animation.",10) == 0) message = value;
}
#include "ProductionHostMode.inc"
namespace emoteplayer {
class emoteengine {
public:
    emoteengine();
    bool integratedAnimation() const { return _integratedAnimation; }
private:
    bool _integratedAnimation = false;
};
#include "ProductionModeConstructor.inc"
}
static int checks = 0;
void require(bool value, const char* description) {
    ++checks; if (!value) throw std::runtime_error(description);
}
int main() {
    try {
        // A stale debug/previous-session value must not override the App's default.
        hint = "integrated";
        require(applyEmoteAnimationModeForStart(), "default mode selection");
        require(hint == "legacy" && lastPriority == SDL_HINT_OVERRIDE, "default explicitly overrides stale hints");
        emoteplayer::emoteengine first;
        require(!first.integratedAnimation(), "default player is legacy");
        MikageKRKRSetExperimentalEmote(true);
        require(hint == "legacy", "changing next-launch preference leaves current hint intact");
        emoteplayer::emoteengine sameSession;
        require(!sameSession.integratedAnimation(), "new players in current session keep current mode");
        require(applyEmoteAnimationModeForStart(), "next launch applies enabled setting");
        emoteplayer::emoteengine second;
        require(second.integratedAnimation() && message == "animation.integrated", "enabled preference reaches production constructor");
        require(!first.integratedAnimation(), "existing player mode stays captured");
        MikageKRKRSetExperimentalEmote(false);
        require(hint == "integrated", "disable is deferred until next launch");
        require(applyEmoteAnimationModeForStart(), "next launch applies disabled setting");
        emoteplayer::emoteengine third;
        require(!third.integratedAnimation() && message == "animation.legacy", "disable clears previous session's mode");
        require(second.integratedAnimation(), "prior player is unaffected by later launch");
        hint.clear();
        require(applyEmoteAnimationModeForStart() && hint == "legacy", "SDL reset is followed by explicit reapplication");
        const char* options[] = {"MIKAGE_EMOTE_NODE_CACHE", "MIKAGE_EMOTE_CAPTURE_CACHE",
            "MIKAGE_EMOTE_LOCAL_UPDATE", "MIKAGE_EMOTE_REGION_COPY", "MIKAGE_EMOTE_ASYNC_ALPHA",
            "MIKAGE_EMOTE_EXPERIMENTAL_BOUNDS", "MIKAGE_EMOTE_LOCAL_POSE_CACHE"};
        for (auto* option : options)
            require(std::strcmp(SDL_GetHint(option), "0") == 0, "performance options default off");
        for (unsigned bit = 0; bit < 7; ++bit) {
            MikageKRKRSetEmotePerformanceOptions(1u << bit);
            require(applyEmoteAnimationModeForStart(), "performance flag applies at next launch");
            for (unsigned index = 0; index < 7; ++index)
                require(emoteplayer::performanceEnabled(options[index]) == (index == bit), "performance bits remain independent");
        }
        MikageKRKRSetEmotePerformanceOptions(0xffffffffu);
        require(applyEmoteAnimationModeForStart() && emotePerformanceOptions.load() == 127u, "unsupported performance bits are masked");
        emoteplayer::performanceCounters().nodeCacheHits.store(99);
        MikageKRKRSetEmotePerformanceOptions(0);
        require(applyEmoteAnimationModeForStart() && emoteplayer::performanceStats().nodeCacheHits == 0, "per-session performance counters reset");
        for (auto* option : options)
            require(!emoteplayer::performanceEnabled(option), "new session clears stale experimental flags");
        hintFails = true;
        require(!applyEmoteAnimationModeForStart(), "hint failure is reported rather than silently selecting wrong mode");
        std::cout << "Passed " << checks << " production host-mode checks.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
