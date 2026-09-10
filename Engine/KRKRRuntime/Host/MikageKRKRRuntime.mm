#include "MikageKRKRRuntime.h"

#import <Foundation/Foundation.h>

#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <string>
#include <exception>
#include <vector>

#include "TVPApplication.h"
#include "tjsError.h"
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

namespace {
bool running = false;
bool foreground = true;
void *appState = nullptr;
std::string lastError;
MikageKRKRMenuCallback menuCallback = nullptr;
MikageKRKRCompletionCallback completionCallback = nullptr;
void *callbackContext = nullptr;

void resetAfterStartFailure(const std::string &message)
{
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
    appState = nullptr;
    menuCallback = nullptr;
    completionCallback = nullptr;
    callbackContext = nullptr;
    lastError = message.empty() ? "KRKR initialization failed." : message;
}

void finish(SDL_AppResult result, const char *message)
{
    if (!running && !appState)
        return;

    SDL_AppQuit(appState, result);
    TVPSetGameRunningOrientation(false);
    MikageKRKRSetWindowScene(nullptr);

    const bool success = result == SDL_APP_SUCCESS;
    if (!success)
        lastError = message && *message ? message : SDL_GetError();

    running = false;
    foreground = true;
    appState = nullptr;

    auto callback = completionCallback;
    auto context = callbackContext;
    menuCallback = nullptr;
    completionCallback = nullptr;
    callbackContext = nullptr;
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
            appState = nullptr;

            SDL_AppResult result = SDL_AppInit(
                &appState,
                static_cast<int>(argv.size()),
                argv.data()
            );
            if (result != SDL_APP_CONTINUE) {
                running = true;
                finish(result, SDL_GetError());
                return false;
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

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SDL_AppResult result = SDL_AppEvent(appState, &event);
        if (result != SDL_APP_CONTINUE)
            return finishForResult(result);
    }

    if (!foreground)
        return MIKAGE_KRKR_STEP_RUNNING;

    return finishForResult(SDL_AppIterate(appState));
}

extern "C" void MikageKRKRRequestStop(void)
{
    if (running && Application)
        Application->Terminate();
}

extern "C" void MikageKRKRSetForeground(bool value)
{
    foreground = value;
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
