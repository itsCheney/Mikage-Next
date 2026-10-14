#include "MikageSDLHost.h"
#include "MikageSDLHostPlatform.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <exception>
#include <string>

namespace {
const MikageSDLHostEngine *engine = nullptr;
bool running = false;
bool foreground = true;
bool stopRequested = false;
int stepDepth = 0;
void *appState = nullptr;
void *windowScene = nullptr;
std::string lastError;
MikageSDLHostMenuCallback menuCallback = nullptr;
MikageSDLHostCompletionCallback completionCallback = nullptr;
void *callbackContext = nullptr;
Uint64 eventTimeNS = 0, iterateTimeNS = 0;

const char *engineName()
{
    return engine && engine->name && *engine->name ? engine->name : "SDL engine";
}

void log(const char *source, int32_t level, const char *message)
{
    if (engine && engine->log)
        engine->log(source, level, message ? message : "");
}

void setAudioSuspended(bool suspended)
{
    if (engine && engine->setAudioSuspended)
        engine->setAudioSuspended(suspended);
}

void sessionEnded()
{
    if (engine && engine->sessionEnded)
        engine->sessionEnded();
    windowScene = nullptr;
}

void finish(SDL_AppResult result, const char *reason)
{
    // reason is often SDL_GetError(), which the engine's SDL_Quit clears.
    const std::string message = reason ? reason : "";
    log("bridge", 3, "runtime.finish.begin");
    if (!message.empty())
        log("bridge", 5, message.c_str());
    if (!running && !appState)
        return;

    try {
        setAudioSuspended(true);
        if (engine->quit)
            result = engine->quit(appState, result);
    } catch (const std::exception &error) {
        result = SDL_APP_FAILURE;
        lastError = error.what() ? error.what()
                                 : std::string("C++ exception during ") + engineName() + " shutdown.";
    } catch (...) {
        result = SDL_APP_FAILURE;
        lastError = std::string("Unknown C++ exception during ") + engineName() + " shutdown.";
    }
    sessionEnded();
    std::string ignored;
    MikageSDLHostPlatformSetAudioSessionActive(false, ignored);

    const bool success = result == SDL_APP_SUCCESS;
    if (!success && lastError.empty())
        lastError = !message.empty() ? message : SDL_GetError();

    running = false;
    foreground = true;
    stopRequested = false;
    appState = nullptr;

    auto callback = completionCallback;
    auto context = callbackContext;
    menuCallback = nullptr;
    completionCallback = nullptr;
    callbackContext = nullptr;
    MikageSDLHostResetFrameStats();
    log("bridge", success ? 3 : 5, success ? "runtime.finish.success" : lastError.c_str());
    if (callback)
        callback(success, success ? nullptr : lastError.c_str(), context);
}

MikageSDLHostStepResult finishForResult(SDL_AppResult result)
{
    return MikageSDLHostFinish(result, result == SDL_APP_FAILURE ? SDL_GetError() : nullptr);
}
}

extern "C" bool MikageSDLHostStart(const MikageSDLHostEngine *newEngine,
                                   const MikageSDLHostConfiguration *configuration)
{
    if (!newEngine || !newEngine->init || !configuration) {
        lastError = "The SDL engine is incomplete.";
        return false;
    }
    if (running) {
        lastError = std::string("A ") + engineName() + " session is already running.";
        return false;
    }
    engine = newEngine;

    // SDL defaults to playback on iOS, which ignores the Ring/Silent switch.
    // The host preference chooses normal game-style ambient behavior or SDL's
    // playback behavior before any audio device opens.
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_CATEGORY,
                            configuration->respectSilentMode ? "ambient" : "playback",
                            SDL_HINT_OVERRIDE);
    log("audio", 3, configuration->respectSilentMode ? "category.ambient" : "category.playback");
    SDL_SetMainReady();
    windowScene = configuration->windowScene;

    menuCallback = configuration->menuCallback;
    completionCallback = configuration->completionCallback;
    callbackContext = configuration->context;
    lastError.clear();
    stopRequested = false;
    MikageSDLHostResetFrameStats();
    appState = nullptr;

    SDL_AppResult result = engine->init(&appState, configuration->argc, configuration->argv);
    if (result != SDL_APP_CONTINUE) {
        running = true;
        finish(result, SDL_GetError());
        return false;
    }
    std::string audioError;
    if (!MikageSDLHostPlatformSetAudioSessionActive(true, audioError)) {
        lastError = audioError;
        setAudioSuspended(true);
    } else {
        setAudioSuspended(false);
    }

    running = true;
    foreground = true;
    MikageSDLHostResetFrameStats(); // Exclude startup/shader compilation from the first gameplay window.
    return true;
}

extern "C" void MikageSDLHostAbortStart(const char *message)
{
    const std::string reason = message ? message : "";
    log("bridge", 5, reason.c_str());
    setAudioSuspended(true);
    if (appState && engine && engine->quit) {
        try {
            engine->quit(appState, SDL_APP_FAILURE);
        } catch (...) {
            // A partially initialized runtime must not throw across the C boundary.
        }
    }
    sessionEnded();
    running = false;
    foreground = true;
    stopRequested = false;
    appState = nullptr;
    menuCallback = nullptr;
    completionCallback = nullptr;
    callbackContext = nullptr;
    MikageSDLHostResetFrameStats();
    lastError = reason.empty() ? std::string(engineName()) + " initialization failed." : reason;
}

extern "C" MikageSDLHostStepResult MikageSDLHostStep(void)
{
    if (!running)
        return MIKAGE_SDL_HOST_STEP_IDLE;
    struct StepDepth {
        StepDepth() { ++stepDepth; }
        ~StepDepth() { --stepDepth; }
    } depth;

    const Uint64 frameStarted = SDL_GetTicksNS();
    const Uint64 eventsStarted = frameStarted;
    Uint64 eventCount = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ++eventCount;
        SDL_AppResult result = engine->event ? engine->event(appState, &event) : SDL_APP_CONTINUE;
        if (result != SDL_APP_CONTINUE)
            return finishForResult(result);
    }
    const Uint64 eventDuration = SDL_GetTicksNS() - eventsStarted;
    eventTimeNS += eventDuration;

    if (stopRequested)
        return finishForResult(SDL_APP_SUCCESS);

    if (!foreground)
        return MIKAGE_SDL_HOST_STEP_RUNNING;

    const Uint64 iterateStarted = SDL_GetTicksNS();
    SDL_AppResult result = engine->iterate ? engine->iterate(appState) : SDL_APP_CONTINUE;
    const Uint64 frameFinished = SDL_GetTicksNS();
    const Uint64 iterateDuration = frameFinished - iterateStarted;
    iterateTimeNS += iterateDuration;
    if (result == SDL_APP_CONTINUE && engine->frameCompleted) {
        const MikageSDLHostFrame frame{frameFinished, frameFinished - frameStarted,
                                       eventDuration, iterateDuration, eventCount};
        engine->frameCompleted(&frame);
    }
    return finishForResult(result);
}

extern "C" MikageSDLHostStepResult MikageSDLHostFinish(SDL_AppResult result, const char *message)
{
    if (result == SDL_APP_CONTINUE)
        return MIKAGE_SDL_HOST_STEP_RUNNING;
    finish(result, message);
    return result == SDL_APP_SUCCESS ? MIKAGE_SDL_HOST_STEP_FINISHED : MIKAGE_SDL_HOST_STEP_FAILED;
}

extern "C" void MikageSDLHostRequestStop(void)
{
    if (running)
        stopRequested = true;
}

extern "C" bool MikageSDLHostSetForeground(bool value)
{
    if (foreground == value)
        return true;
    std::string error;
    if (!value) {
        foreground = false;
        if (engine && engine->setActive)
            engine->setActive(false);
        setAudioSuspended(true);
        const bool deactivated = MikageSDLHostPlatformSetAudioSessionActive(false, error);
        lastError = deactivated ? std::string() : error;
        log("audio", deactivated ? 3 : 5, deactivated ? "foreground.suspended" : lastError.c_str());
        return deactivated;
    }

    if (!MikageSDLHostPlatformSetAudioSessionActive(true, error)) {
        lastError = error;
        log("audio", 5, lastError.c_str());
        return false;
    }
    setAudioSuspended(false);
    if (engine && engine->setActive)
        engine->setActive(true);
    foreground = true;
    MikageSDLHostResetFrameStats(); // Background time is not a gameplay frame interval.
    lastError.clear();
    log("audio", 3, "foreground.resumed");
    return true;
}

extern "C" void MikageSDLHostSetForegroundState(bool value)
{
    foreground = value;
}

extern "C" void MikageSDLHostResetFrameStats(void)
{
    eventTimeNS = iterateTimeNS = 0;
    if (engine && engine->resetFrameStats)
        engine->resetFrameStats();
}

extern "C" void MikageSDLHostNotifyMenu(void)
{
    if (menuCallback)
        menuCallback(callbackContext);
}

extern "C" bool MikageSDLHostSendBackKey(void)
{
    if (!running)
        return false;
    SDL_Window *window = SDL_GetKeyboardFocus();
    if (!window) {
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (windows && count > 0)
            window = windows[0];
        SDL_free(windows);
    }
    SDL_Event event{};
    event.key.timestamp = SDL_GetTicksNS();
    event.key.windowID = window ? SDL_GetWindowID(window) : 0;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.key = SDLK_ESCAPE;
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    if (!SDL_PushEvent(&event))
        return false;
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    return SDL_PushEvent(&event);
}

extern "C" bool MikageSDLHostIsRunning(void)
{
    return running;
}

extern "C" bool MikageSDLHostIsForeground(void)
{
    return foreground;
}

extern "C" bool MikageSDLHostIsStepping(void)
{
    return stepDepth != 0;
}

extern "C" void *MikageSDLHostWindowScene(void)
{
    return windowScene;
}

extern "C" uint64_t MikageSDLHostEventTimeNS(void)
{
    return eventTimeNS;
}

extern "C" uint64_t MikageSDLHostIterateTimeNS(void)
{
    return iterateTimeNS;
}

extern "C" const char *MikageSDLHostLastError(void)
{
    return lastError.c_str();
}

extern "C" void MikageSDLHostSetLastError(const char *message)
{
    lastError = message ? message : "";
}

extern "C" void *MikageSDLHostUIKitWindow(SDL_Window *window)
{
    if (!window) {
        int count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (windows && count > 0)
            window = windows[0];
        SDL_free(windows);
    }
    if (!window)
        return nullptr;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                  SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,
                                  nullptr);
}
