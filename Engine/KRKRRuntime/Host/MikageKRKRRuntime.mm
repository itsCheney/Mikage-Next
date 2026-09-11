#include "MikageKRKRRuntime.h"

#import <Foundation/Foundation.h>
#import <AVFAudio/AVFAudio.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <string>
#include <exception>
#include <cstring>
#include <vector>

#include "TVPApplication.h"
#include "tjsError.h"

extern void TVPSetAudioSuspended(bool suspended);
@interface MikageKRKRBundleMarker : NSObject
@end
@implementation MikageKRKRBundleMarker
@end

extern "C" NSString *MikageKRKRFrameworkResourcePath(void)
{
    return [[NSBundle bundleForClass:MikageKRKRBundleMarker.class] resourcePath];
}

extern "C" SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]);
extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
extern "C" SDL_AppResult SDL_AppIterate(void *appstate);
extern "C" void SDL_AppQuit(void *appstate, SDL_AppResult result);
extern tTVPApplication *Application;
extern "C" void TVPSetGameRunningOrientation(bool running);
extern "C" void MikageKRKRSetWindowScene(void *scene);
extern "C" void MikageKRKRSetMenuGestureEnabled(bool enabled);
extern "C" SDL_Window *MikageKRKRGetSDLWindow(void);
extern "C" const char *MikageKRKRGetActiveRendererName(void);

namespace {
bool running = false;
bool foreground = true;
bool stopRequested = false;
void *appState = nullptr;
std::string lastError;
MikageKRKRMenuCallback menuCallback = nullptr;
MikageKRKRCompletionCallback completionCallback = nullptr;
void *callbackContext = nullptr;
std::string activeRenderer;
Uint64 statsWindowStarted = 0;
Uint64 previousFrameAt = 0;
Uint64 frameIntervalTotal = 0;
Uint64 frameCount = 0;
double currentFPS = 0;
double currentFrameTimeMS = 0;
constexpr Uint64 nanosecondsPerSecond = 1000000000ULL;

void resetStats()
{
    statsWindowStarted = SDL_GetTicksNS();
    previousFrameAt = 0;
    frameIntervalTotal = 0;
    frameCount = 0;
    currentFPS = 0;
    currentFrameTimeMS = 0;
}

void recordFrame(Uint64 presentedAt)
{
    if (!statsWindowStarted)
        statsWindowStarted = presentedAt;
    if (previousFrameAt)
        frameIntervalTotal += presentedAt - previousFrameAt;
    previousFrameAt = presentedAt;
    frameCount++;
    Uint64 elapsed = presentedAt - statsWindowStarted;
    if (elapsed >= nanosecondsPerSecond)
    {
        currentFPS =
            static_cast<double>(frameCount) * nanosecondsPerSecond / elapsed;
        if (frameCount > 1)
            currentFrameTimeMS =
                static_cast<double>(frameIntervalTotal) / (frameCount - 1) / 1000000.0;
        statsWindowStarted = presentedAt;
        previousFrameAt = 0;
        frameIntervalTotal = 0;
        frameCount = 0;
    }
}

void resetAfterStartFailure(const std::string &message)
{
    TVPSetAudioSuspended(true);
    if (appState) {
        try {
            SDL_AppQuit(appState, SDL_APP_FAILURE);
        } catch (...) {
            // A partially initialized runtime must not throw across the C boundary.
        }
    }
    TVPSetGameRunningOrientation(false);
    MikageKRKRSetWindowScene(nullptr);
    running = false;
    foreground = true;
    stopRequested = false;
    appState = nullptr;
    menuCallback = nullptr;
    completionCallback = nullptr;
    callbackContext = nullptr;
    activeRenderer.clear();
    resetStats();
    lastError = message.empty() ? "KRKR initialization failed." : message;
}

void finish(SDL_AppResult result, const char *message)
{
    if (!running && !appState)
        return;

    try {
        TVPSetAudioSuspended(true);
        SDL_AppQuit(appState, result);
    } catch (const eTJS &error) {
        result = SDL_APP_FAILURE;
        lastError = error.GetMessage().c_str();
    } catch (const std::exception &error) {
        result = SDL_APP_FAILURE;
        lastError = error.what() ? error.what() : "C++ exception during KRKR shutdown.";
    } catch (...) {
        result = SDL_APP_FAILURE;
        lastError = "Unknown C++ exception during KRKR shutdown.";
    }
    TVPSetGameRunningOrientation(false);
    MikageKRKRSetWindowScene(nullptr);
    [[AVAudioSession sharedInstance]
        setActive:NO
        withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
        error:nil];

    const bool success = result == SDL_APP_SUCCESS;
    if (!success && lastError.empty())
        lastError = message && *message ? message : SDL_GetError();

    running = false;
    foreground = true;
    stopRequested = false;
    appState = nullptr;

    auto callback = completionCallback;
    auto context = callbackContext;
    menuCallback = nullptr;
    completionCallback = nullptr;
    callbackContext = nullptr;
    activeRenderer.clear();
    resetStats();
    if (callback)
        callback(success, success ? nullptr : lastError.c_str(), context);
}

MikageKRKRStepResult finishForResult(SDL_AppResult result)
{
    if (result == SDL_APP_CONTINUE)
        return MIKAGE_KRKR_STEP_RUNNING;
    const char *message = result == SDL_APP_FAILURE ? SDL_GetError() : nullptr;
    finish(result, message);
    return result == SDL_APP_SUCCESS ? MIKAGE_KRKR_STEP_FINISHED : MIKAGE_KRKR_STEP_FAILED;
}
}

extern "C" bool MikageKRKRStart(const char *gamePath,
                                  const char *renderer,
                                  void *uiWindowScene,
                                  bool menuGestureEnabled,
                                  MikageKRKRMenuCallback menu,
                                  MikageKRKRCompletionCallback completion,
                                  void *context)
{
    if (running) {
        lastError = "A KRKR session is already running.";
        return false;
    }
    if (!gamePath || !*gamePath) {
        lastError = "The KRKR game path is empty.";
        return false;
    }

    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:gamePath];
        BOOL isDirectory = NO;
        if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDirectory]) {
            lastError = "The KRKR game path does not exist.";
            return false;
        }

        std::string normalizedPath(gamePath);
        if (isDirectory && normalizedPath.back() != '/')
            normalizedPath.push_back('/');

        try {
            SDL_SetMainReady();
            MikageKRKRSetWindowScene(uiWindowScene);
            MikageKRKRSetMenuGestureEnabled(menuGestureEnabled);
            TVPSetGameRunningOrientation(true);

            std::vector<std::string> arguments;
            arguments.emplace_back("MikageNext");
            arguments.emplace_back(normalizedPath);
            arguments.emplace_back("-forcelog=yes");
            if (renderer && *renderer)
                arguments.emplace_back(std::string("-render=") + renderer);

            std::vector<char *> argv;
            argv.reserve(arguments.size());
            for (auto &argument : arguments)
                argv.push_back(argument.data());

            menuCallback = menu;
            completionCallback = completion;
            callbackContext = context;
            lastError.clear();
            activeRenderer = renderer && *renderer ? renderer : "metal";
            stopRequested = false;
            resetStats();
            appState = nullptr;

            SDL_AppResult result = SDL_AppInit(
                &appState,
                static_cast<int>(argv.size()),
                argv.data()
            );
            const char *actualRenderer = MikageKRKRGetActiveRendererName();
            if (actualRenderer && *actualRenderer)
                activeRenderer = actualRenderer;
            if (result != SDL_APP_CONTINUE) {
                running = true;
                finish(result, SDL_GetError());
                return false;
            }
            NSError *audioError = nil;
            [[AVAudioSession sharedInstance] setActive:YES error:&audioError];
            if (audioError) {
                lastError = audioError.localizedDescription.UTF8String;
                TVPSetAudioSuspended(true);
            } else {
                TVPSetAudioSuspended(false);
            }
        } catch (const eTJS &error) {
            resetAfterStartFailure(std::string(error.GetMessage().c_str()));
            return false;
        } catch (const std::exception &error) {
            resetAfterStartFailure(error.what() ? error.what() : "C++ exception during KRKR startup.");
            return false;
        } catch (...) {
            resetAfterStartFailure("Unknown C++ exception during KRKR startup.");
            return false;
        }
    }

    running = true;
    foreground = true;
    return true;
}

extern "C" MikageKRKRStepResult MikageKRKRStep(void)
{
    if (!running)
        return MIKAGE_KRKR_STEP_IDLE;

    try {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            SDL_AppResult result = SDL_AppEvent(appState, &event);
            if (result != SDL_APP_CONTINUE)
                return finishForResult(result);
        }

        if (stopRequested)
            return finishForResult(SDL_APP_SUCCESS);

        if (!foreground)
            return MIKAGE_KRKR_STEP_RUNNING;

        SDL_AppResult result = SDL_AppIterate(appState);
        if (result == SDL_APP_CONTINUE)
            recordFrame(SDL_GetTicksNS());
        return finishForResult(result);
    } catch (const eTJS &error) {
        std::string message(error.GetMessage().c_str());
        finish(SDL_APP_FAILURE, message.c_str());
    } catch (const std::exception &error) {
        finish(SDL_APP_FAILURE, error.what());
    } catch (...) {
        finish(SDL_APP_FAILURE, "Unknown C++ exception while stepping KRKR.");
    }
    return MIKAGE_KRKR_STEP_FAILED;
}

extern "C" void MikageKRKRRequestStop(void)
{
    try {
        if (running) {
            stopRequested = true;
            if (Application)
                Application->Terminate();
        }
    } catch (...) {
        finish(SDL_APP_FAILURE, "C++ exception while stopping KRKR.");
    }
}

extern "C" bool MikageKRKRSetForeground(bool value)
{
    try {
        if (foreground == value)
            return true;
        if (!value) {
            foreground = false;
            if (Application)
                Application->NotifyActiveEvent(eTVPActiveEvent::onDeactive);
            TVPSetAudioSuspended(true);
            NSError *error = nil;
            [[AVAudioSession sharedInstance]
                setActive:NO
                withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                error:&error];
            if (error)
                lastError = error.localizedDescription.UTF8String;
            else
                lastError.clear();
            return error == nil;
        }

        NSError *error = nil;
        [[AVAudioSession sharedInstance] setActive:YES error:&error];
        if (error) {
            lastError = error.localizedDescription.UTF8String;
            return false;
        }
        TVPSetAudioSuspended(false);
        if (Application)
            Application->NotifyActiveEvent(eTVPActiveEvent::onActive);
        foreground = value;
        lastError.clear();
        return true;
    } catch (const eTJS &error) {
        lastError = error.GetMessage().c_str();
    } catch (const std::exception &error) {
        lastError = error.what() ? error.what() : "C++ exception while changing KRKR foreground state.";
    } catch (...) {
        lastError = "Unknown C++ exception while changing KRKR foreground state.";
    }
    TVPSetAudioSuspended(true);
    foreground = false;
    return false;
}

extern "C" bool MikageKRKRGetStats(MikageKRKRStats *stats)
{
    if (!running || !stats)
        return false;
    std::memset(stats, 0, sizeof(*stats));
    stats->framesPerSecond = currentFPS;
    stats->frameTimeMilliseconds = currentFrameTimeMS;
    if (SDL_Window *window = MikageKRKRGetSDLWindow()) {
        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        stats->drawableWidth = width;
        stats->drawableHeight = height;
    }
    std::strncpy(stats->renderer, activeRenderer.c_str(), sizeof(stats->renderer) - 1);
    return true;
}

extern "C" bool MikageKRKRIsRunning(void)
{
    return running;
}

extern "C" const char *MikageKRKRLastError(void)
{
    return lastError.c_str();
}

extern "C" void *MikageKRKRNativeWindow(void)
{
    SDL_Window *window = MikageKRKRGetSDLWindow();
    if (!window)
        return nullptr;
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                  SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,
                                  nullptr);
}

extern "C" void MikageKRKRNotifyMenu(void)
{
    if (menuCallback)
        menuCallback(callbackContext);
}
