#include "MikageKRKRRuntime.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

enum SDL_HintPriority { SDL_HINT_DEFAULT, SDL_HINT_NORMAL, SDL_HINT_OVERRIDE };
static std::string hint, message;
static SDL_HintPriority lastPriority = SDL_HINT_DEFAULT;
static bool hintFails = false;
bool SDL_SetHintWithPriority(const char* name, const char* value, SDL_HintPriority priority) {
    if (std::strcmp(name, "MIKAGE_EMOTE_ANIMATION_MODE")) throw std::runtime_error("wrong hint");
    if (hintFails) return false;
    hint = value; lastPriority = priority; return true;
}
const char* SDL_GetHint(const char*) { return hint.empty() ? nullptr : hint.c_str(); }
void MikageKRKRLogMessage(const char*, int32_t, const char* value) { message = value; }
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
        hintFails = true;
        require(!applyEmoteAnimationModeForStart(), "hint failure is reported rather than silently selecting wrong mode");
        std::cout << "Passed " << checks << " production host-mode checks.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
