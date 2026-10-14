#pragma once

// Mikage SDL host: the engine-neutral half of running an SDL3 main-callback
// engine inside the app. The app drives the session one step per display
// frame; the host owns the SDL event pump, stop/foreground gating, the
// AVAudioSession, the UIKit window scene and the end-of-session reset, so the
// next game (of any SDL engine) starts from a clean SDL.
//
// Each engine runtime compiles these sources into its own framework and keeps
// its public bridge (MikageKRKR*, ...). The symbols are hidden, so several
// runtimes can carry a copy; only one session runs at a time. Engines that do
// not use SDL stay at the app's engine registry and never touch this layer.
//
// All functions are called on the main thread.

#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#if defined(__GNUC__) || defined(__clang__)
#define MIKAGE_SDL_HOST_API __attribute__((visibility("hidden")))
#else
#define MIKAGE_SDL_HOST_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MikageSDLHostStepResult {
    MIKAGE_SDL_HOST_STEP_IDLE = 0,
    MIKAGE_SDL_HOST_STEP_RUNNING = 1,
    MIKAGE_SDL_HOST_STEP_FINISHED = 2,
    MIKAGE_SDL_HOST_STEP_FAILED = 3
} MikageSDLHostStepResult;

typedef void (*MikageSDLHostMenuCallback)(void *context);
typedef void (*MikageSDLHostCompletionCallback)(bool success, const char *message, void *context);

typedef struct MikageSDLHostFrame {
    uint64_t finishedNS;  // SDL_GetTicksNS when the iterate callback returned
    uint64_t workNS;      // whole step: events plus iterate
    uint64_t eventNS;
    uint64_t iterateNS;
    uint64_t eventCount;
} MikageSDLHostFrame;

// What an engine gives the host. The four SDL callbacks follow SDL's
// main-callback contract (SDL_AppInit/Event/Iterate/Quit). Every hook may be
// NULL. Hooks may throw; the host then leaves the exception to the engine's
// own bridge, which reports it through MikageSDLHostFinish/AbortStart.
typedef struct MikageSDLHostEngine {
    const char *name;  // "KRKR"; used in host messages
    SDL_AppResult (*init)(void **appstate, int argc, char *argv[]);
    SDL_AppResult (*event)(void *appstate, SDL_Event *event);
    SDL_AppResult (*iterate)(void *appstate);
    // Returns the result the session ends with; an engine that fails while
    // quitting sets the host error and returns SDL_APP_FAILURE.
    SDL_AppResult (*quit)(void *appstate, SDL_AppResult result);
    // Runs after quit (or an aborted start), before the audio session ends.
    void (*sessionEnded)(void);
    // Pauses or resumes the engine's own mixer around audio session changes.
    void (*setAudioSuspended)(bool suspended);
    // Tells the engine the app moved to or from the foreground. Runs after
    // the engine stops being stepped, and before it is stepped again.
    void (*setActive)(bool active);
    void (*frameCompleted)(const MikageSDLHostFrame *frame);
    // Frame statistics restart (session start/end, foreground resume).
    void (*resetFrameStats)(void);
    void (*log)(const char *source, int32_t level, const char *message);
} MikageSDLHostEngine;

typedef struct MikageSDLHostConfiguration {
    int argc;
    char **argv;
    // UIWindowScene the engine's SDL window should join. SDL falls back to
    // the active scene when an engine does not pass it at window creation.
    void *windowScene;
    // Ambient audio follows the Ring/Silent switch; playback ignores it.
    bool respectSilentMode;
    MikageSDLHostMenuCallback menuCallback;
    MikageSDLHostCompletionCallback completionCallback;
    void *context;
} MikageSDLHostConfiguration;

// Runs the engine's init callback. On an init result other than
// SDL_APP_CONTINUE the session is finished (and its completion reported)
// before this returns false. The engine must outlive the session.
MIKAGE_SDL_HOST_API bool MikageSDLHostStart(const MikageSDLHostEngine *engine,
                                            const MikageSDLHostConfiguration *configuration);
// Clears a start that threw out of the engine's init callback. No completion
// callback runs; message becomes the last error.
MIKAGE_SDL_HOST_API void MikageSDLHostAbortStart(const char *message);
// Pumps SDL events into the engine, then iterates it once while foreground.
MIKAGE_SDL_HOST_API MikageSDLHostStepResult MikageSDLHostStep(void);
// Ends the session with result (SDL_APP_CONTINUE keeps it running) and
// reports completion. message, when set, is logged and becomes the error.
MIKAGE_SDL_HOST_API MikageSDLHostStepResult MikageSDLHostFinish(SDL_AppResult result, const char *message);
// The next step quits the engine with success.
MIKAGE_SDL_HOST_API void MikageSDLHostRequestStop(void);
// Suspends or resumes stepping, the engine's audio and the audio session.
MIKAGE_SDL_HOST_API bool MikageSDLHostSetForeground(bool foreground);
// Records a foreground change the engine completed itself.
MIKAGE_SDL_HOST_API void MikageSDLHostSetForegroundState(bool foreground);
MIKAGE_SDL_HOST_API void MikageSDLHostResetFrameStats(void);
MIKAGE_SDL_HOST_API void MikageSDLHostNotifyMenu(void);
// Queues an Escape key press and release for the engine's focused window,
// for games that only offer a way back through the keyboard. False when no
// session is running.
MIKAGE_SDL_HOST_API bool MikageSDLHostSendBackKey(void);

MIKAGE_SDL_HOST_API bool MikageSDLHostIsRunning(void);
MIKAGE_SDL_HOST_API bool MikageSDLHostIsForeground(void);
// True while MikageSDLHostStep is on the stack. A modal SDL dialog pumps the
// run loop mid-step, so host calls can arrive inside one.
MIKAGE_SDL_HOST_API bool MikageSDLHostIsStepping(void);
MIKAGE_SDL_HOST_API void *MikageSDLHostWindowScene(void);
// Session-cumulative time spent pumping events and in the iterate callback.
MIKAGE_SDL_HOST_API uint64_t MikageSDLHostEventTimeNS(void);
MIKAGE_SDL_HOST_API uint64_t MikageSDLHostIterateTimeNS(void);
MIKAGE_SDL_HOST_API const char *MikageSDLHostLastError(void);
MIKAGE_SDL_HOST_API void MikageSDLHostSetLastError(const char *message);
// The UIWindow behind window, or behind the first SDL window when NULL.
MIKAGE_SDL_HOST_API void *MikageSDLHostUIKitWindow(SDL_Window *window);

#ifdef __cplusplus
}
#endif
