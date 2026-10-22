#include "MikageSDLHost.h"
#include "MikageSDLHostPlatform.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Everything the host does to the engine, the audio session and the app,
// in order.
std::vector<std::string> calls;
bool audioSessionRefuses = false;

void Require(bool ok, const char *expression, int line)
{
    if (ok)
        return;
    std::fprintf(stderr, "line %d: %s\n", line, expression);
    for (const auto &call : calls)
        std::fprintf(stderr, "  %s\n", call.c_str());
    std::exit(1);
}
#define REQUIRE(expression) Require((expression), #expression, __LINE__)

bool Called(const std::string &call)
{
    for (const auto &entry : calls)
        if (entry == call)
            return true;
    return false;
}

// Index of call at or after from; calls.size() when missing.
size_t Find(const std::string &call, size_t from = 0)
{
    for (size_t i = from; i < calls.size(); ++i)
        if (calls[i] == call)
            return i;
    return calls.size();
}

bool InOrder(std::initializer_list<const char *> sequence)
{
    size_t at = 0;
    for (const char *call : sequence) {
        at = Find(call, at);
        if (at == calls.size())
            return false;
        ++at;
    }
    return true;
}

struct Script {
    SDL_AppResult initResult = SDL_APP_CONTINUE;
    bool initCreatesState = true;
    bool initThrows = false;
    SDL_AppResult iterateResult = SDL_APP_CONTINUE;
    const char *iterateError = nullptr;
    SDL_AppResult eventResult = SDL_APP_CONTINUE;
    bool quitThrows = false;
    bool quitFails = false;
    bool requestStopInIterate = false;
    bool sawSteppingInIterate = false;
    int events = 0;
    int iterations = 0;
    int frames = 0;
    std::vector<std::string> argv;
} script;

int stateToken = 42;

SDL_AppResult Init(void **appstate, int argc, char *argv[])
{
    calls.emplace_back("init");
    script.argv.assign(argv, argv + argc);
    if (!SDL_Init(SDL_INIT_EVENTS))
        return SDL_APP_FAILURE;
    if (script.initCreatesState)
        *appstate = &stateToken;
    if (script.initThrows)
        throw std::runtime_error("init threw");
    if (script.initResult == SDL_APP_FAILURE)
        SDL_SetError("init refused");
    return script.initResult;
}

SDL_AppResult Event(void *appstate, SDL_Event *event)
{
    REQUIRE(appstate == &stateToken);
    if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
        event->key.key == SDLK_ESCAPE && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        calls.emplace_back(event->key.down ? "event.escape.down" : "event.escape.up");
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_USER) {
        ++script.events;
        calls.emplace_back("event.user");
        return script.eventResult;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult Iterate(void *appstate)
{
    REQUIRE(appstate == &stateToken);
    ++script.iterations;
    calls.emplace_back("iterate");
    script.sawSteppingInIterate = MikageSDLHostIsStepping();
    if (script.requestStopInIterate)
        MikageSDLHostRequestStop();
    if (script.iterateError)
        SDL_SetError("%s", script.iterateError);
    return script.iterateResult;
}

SDL_AppResult Quit(void *appstate, SDL_AppResult result)
{
    calls.emplace_back(std::string("quit.") +
                       (result == SDL_APP_SUCCESS ? "success" : "failure") +
                       (appstate == &stateToken ? "" : ".nostate"));
    SDL_Quit();
    if (script.quitThrows)
        throw std::runtime_error("quit threw");
    if (script.quitFails) {
        MikageSDLHostSetLastError("engine failed while quitting");
        return SDL_APP_FAILURE;
    }
    return result;
}

void SessionEnded() { calls.emplace_back("sessionEnded"); }
void SetAudioSuspended(bool suspended) { calls.emplace_back(suspended ? "engineAudio.suspend" : "engineAudio.resume"); }
void SetActive(bool active) { calls.emplace_back(active ? "active" : "inactive"); }
void FrameCompleted(const MikageSDLHostFrame *frame)
{
    ++script.frames;
    REQUIRE(frame->workNS >= frame->iterateNS);
    REQUIRE(frame->workNS >= frame->eventNS);
    REQUIRE(frame->finishedNS > 0);
}
void ResetFrameStats() { calls.emplace_back("resetFrameStats"); }
std::vector<std::string> logs;
void Log(const char *source, int32_t level, const char *message)
{
    logs.push_back(std::string(source) + ":" + std::to_string(level) + ":" + message);
}

const MikageSDLHostEngine engine = {
    "Test", Init, Event, Iterate, Quit, SessionEnded, SetAudioSuspended,
    SetActive, FrameCompleted, ResetFrameStats, Log,
};

struct Completion {
    int count = 0;
    bool success = false;
    std::string message;
} completion;
int menus = 0;

void OnComplete(bool success, const char *message, void *context)
{
    REQUIRE(context == &completion);
    calls.emplace_back("completion");
    ++completion.count;
    completion.success = success;
    completion.message = message ? message : "";
    // The session is fully reset before completion runs, so the app may start
    // the next game from inside it.
    REQUIRE(!MikageSDLHostIsRunning());
}

void OnMenu(void *context)
{
    REQUIRE(context == &completion);
    ++menus;
}

int sceneToken = 7;
char argument0[] = "Mikage";
char argument1[] = "/game/";
char *arguments[] = {argument0, argument1};

bool Start(bool respectSilentMode = true)
{
    calls.clear();
    logs.clear();
    completion = {};
    const MikageSDLHostConfiguration configuration = {
        2, arguments, &sceneToken, respectSilentMode, OnMenu, OnComplete, &completion,
    };
    return MikageSDLHostStart(&engine, &configuration);
}

void PushUserEvent()
{
    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    REQUIRE(SDL_PushEvent(&event));
}

void Reset()
{
    script = {};
    audioSessionRefuses = false;
    menus = 0;
}

void RunsAndStopsNormally()
{
    Reset();
    REQUIRE(Start(true));
    REQUIRE(MikageSDLHostIsRunning() && MikageSDLHostIsForeground());
    REQUIRE((script.argv == std::vector<std::string>{"Mikage", "/game/"}));
    REQUIRE(std::strcmp(SDL_GetHint(SDL_HINT_AUDIO_CATEGORY), "ambient") == 0);
    REQUIRE(MikageSDLHostWindowScene() == &sceneToken);
    REQUIRE(InOrder({"resetFrameStats", "init", "audioSession.activate", "engineAudio.resume", "resetFrameStats"}));
    REQUIRE(*MikageSDLHostLastError() == 0);

    calls.clear();
    PushUserEvent();
    PushUserEvent();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_RUNNING);
    REQUIRE(script.events == 2 && script.iterations == 1 && script.frames == 1);
    REQUIRE(InOrder({"event.user", "event.user", "iterate"}));
    REQUIRE(script.sawSteppingInIterate && !MikageSDLHostIsStepping());

    MikageSDLHostNotifyMenu();
    REQUIRE(menus == 1);

    MikageSDLHostRequestStop();
    calls.clear();
    PushUserEvent();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    // Pending events still reach the engine; it is not iterated again.
    REQUIRE(script.events == 3 && script.iterations == 1);
    REQUIRE(InOrder({"event.user", "engineAudio.suspend", "quit.success", "sessionEnded",
                     "audioSession.deactivate", "resetFrameStats", "completion"}));
    REQUIRE(completion.count == 1 && completion.success && completion.message.empty());
    REQUIRE(!MikageSDLHostIsRunning() && MikageSDLHostIsForeground());
    REQUIRE(MikageSDLHostWindowScene() == nullptr);
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_IDLE);
    REQUIRE(completion.count == 1);
    // Menu requests after the session ends go nowhere.
    MikageSDLHostNotifyMenu();
    REQUIRE(menus == 1);
}

void StopsFromInsideIterate()
{
    Reset();
    REQUIRE(Start(false));
    REQUIRE(std::strcmp(SDL_GetHint(SDL_HINT_AUDIO_CATEGORY), "playback") == 0);
    script.requestStopInIterate = true;
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_RUNNING);
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    REQUIRE(script.iterations == 1 && completion.success);
}

void BackgroundStopsIterating()
{
    Reset();
    REQUIRE(Start());
    calls.clear();
    REQUIRE(MikageSDLHostSetForeground(false));
    REQUIRE(!MikageSDLHostIsForeground());
    REQUIRE(InOrder({"inactive", "engineAudio.suspend"}));
    // Deactivating the session would stop SDL's audio queue for good.
    REQUIRE(!Called("audioSession.deactivate"));
    REQUIRE(MikageSDLHostSetForeground(false));  // Already there: no second suspend.
    REQUIRE(Find("inactive", Find("inactive") + 1) == calls.size());

    PushUserEvent();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_RUNNING);
    REQUIRE(script.events == 1 && script.iterations == 0);

    // The system refuses the audio session: the game stays in the background.
    audioSessionRefuses = true;
    REQUIRE(!MikageSDLHostSetForeground(true));
    REQUIRE(!MikageSDLHostIsForeground());
    REQUIRE(std::strcmp(MikageSDLHostLastError(), "audio session refused") == 0);
    REQUIRE(!Called("active"));

    audioSessionRefuses = false;
    calls.clear();
    REQUIRE(MikageSDLHostSetForeground(true));
    REQUIRE(MikageSDLHostIsForeground() && *MikageSDLHostLastError() == 0);
    REQUIRE(InOrder({"audioSession.activate", "engineAudio.resume", "active", "resetFrameStats"}));
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_RUNNING);
    REQUIRE(script.iterations == 1);

    // Stopping while in the background still quits on the next step.
    REQUIRE(MikageSDLHostSetForeground(false));
    MikageSDLHostRequestStop();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    REQUIRE(script.iterations == 1 && completion.success);
    REQUIRE(MikageSDLHostIsForeground());
}

void IterateFailureReportsSDLError()
{
    Reset();
    REQUIRE(Start());
    script.iterateResult = SDL_APP_FAILURE;
    script.iterateError = "scenario crashed";
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FAILED);
    REQUIRE(script.frames == 0);
    REQUIRE(Called("quit.failure"));
    REQUIRE(completion.count == 1 && !completion.success);
    REQUIRE(completion.message == "scenario crashed");
    REQUIRE(std::string(MikageSDLHostLastError()) == "scenario crashed");
}

void EventCanEndTheGame()
{
    Reset();
    REQUIRE(Start());
    script.eventResult = SDL_APP_SUCCESS;
    PushUserEvent();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    REQUIRE(script.iterations == 0 && completion.success);
}

void InitFailureFinishesBeforeReturning()
{
    Reset();
    script.initResult = SDL_APP_FAILURE;
    REQUIRE(!Start());
    REQUIRE(!MikageSDLHostIsRunning());
    REQUIRE(InOrder({"init", "engineAudio.suspend", "quit.failure", "sessionEnded", "completion"}));
    REQUIRE(!Called("audioSession.activate"));
    REQUIRE(completion.count == 1 && completion.message == "init refused");

    // An engine may also end cleanly during init (the game quit at once).
    Reset();
    script.initResult = SDL_APP_SUCCESS;
    REQUIRE(!Start());
    REQUIRE(completion.count == 1 && completion.success);
}

void QuitFailuresBecomeTheResult()
{
    Reset();
    REQUIRE(Start());
    script.quitFails = true;
    MikageSDLHostRequestStop();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    REQUIRE(!completion.success && completion.message == "engine failed while quitting");

    Reset();
    REQUIRE(Start());
    script.quitThrows = true;
    MikageSDLHostRequestStop();
    MikageSDLHostStep();
    REQUIRE(!completion.success && completion.message == "quit threw");
    REQUIRE(Called("sessionEnded") && Called("audioSession.deactivate"));
    REQUIRE(!MikageSDLHostIsRunning());
}

void AbortedStartResetsWithoutCompletion()
{
    Reset();
    script.initThrows = true;
    bool threw = false;
    try {
        Start();
    } catch (const std::runtime_error &) {
        threw = true;
        MikageSDLHostAbortStart("The game exited during startup.");
    }
    REQUIRE(threw);
    REQUIRE(InOrder({"init", "engineAudio.suspend", "quit.failure", "sessionEnded", "resetFrameStats"}));
    REQUIRE(completion.count == 0);
    REQUIRE(!MikageSDLHostIsRunning() && MikageSDLHostWindowScene() == nullptr);
    REQUIRE(std::string(MikageSDLHostLastError()) == "The game exited during startup.");
    MikageSDLHostNotifyMenu();
    REQUIRE(menus == 0);

    // Before init produced any state, abort does not quit the engine.
    Reset();
    script.initThrows = true;
    script.initCreatesState = false;
    try {
        Start();
    } catch (const std::runtime_error &) {
        MikageSDLHostAbortStart(nullptr);
    }
    REQUIRE(!Called("quit.failure") && !Called("quit.failure.nostate"));
    REQUIRE(Called("engineAudio.suspend") && Called("sessionEnded"));
    REQUIRE(std::string(MikageSDLHostLastError()) == "Test initialization failed.");
    SDL_Quit();
}

void RefusedAudioSessionStillStarts()
{
    Reset();
    audioSessionRefuses = true;
    REQUIRE(Start());
    REQUIRE(std::string(MikageSDLHostLastError()) == "audio session refused");
    REQUIRE(InOrder({"init", "audioSession.activate.refused", "engineAudio.suspend"}));
    REQUIRE(!Called("engineAudio.resume"));
    audioSessionRefuses = false;
    MikageSDLHostRequestStop();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
}

void BackKeyReachesTheEngine()
{
    Reset();
    REQUIRE(!MikageSDLHostSendBackKey());
    REQUIRE(Start());
    calls.clear();
    REQUIRE(MikageSDLHostSendBackKey());
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_RUNNING);
    REQUIRE(InOrder({"event.escape.down", "event.escape.up", "iterate"}));
    MikageSDLHostRequestStop();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    REQUIRE(!MikageSDLHostSendBackKey());
}

void OneSessionAtATime()
{
    Reset();
    REQUIRE(Start());
    const MikageSDLHostConfiguration configuration = {2, arguments, nullptr, true, nullptr, nullptr, nullptr};
    REQUIRE(!MikageSDLHostStart(&engine, &configuration));
    REQUIRE(std::string(MikageSDLHostLastError()) == "A Test session is already running.");
    REQUIRE(MikageSDLHostIsRunning());
    MikageSDLHostRequestStop();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
    REQUIRE(completion.count == 1);
    // Nothing left over: a finish with no session reports nothing.
    REQUIRE(MikageSDLHostFinish(SDL_APP_FAILURE, "late") == MIKAGE_SDL_HOST_STEP_FAILED);
    REQUIRE(completion.count == 1);
    REQUIRE(MikageSDLHostFinish(SDL_APP_CONTINUE, nullptr) == MIKAGE_SDL_HOST_STEP_RUNNING);
}

void SecondSessionStartsClean()
{
    Reset();
    REQUIRE(Start());
    script.requestStopInIterate = true;
    MikageSDLHostStep();
    MikageSDLHostStep();
    REQUIRE(completion.success && !SDL_WasInit(SDL_INIT_EVENTS));
    Reset();
    REQUIRE(Start());
    REQUIRE(SDL_WasInit(SDL_INIT_EVENTS));
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_RUNNING);
    REQUIRE(script.events == 0 && script.iterations == 1);
    MikageSDLHostRequestStop();
    REQUIRE(MikageSDLHostStep() == MIKAGE_SDL_HOST_STEP_FINISHED);
}
}

bool MikageSDLHostPlatformSetAudioSessionActive(bool active, std::string &error)
{
    if (audioSessionRefuses) {
        calls.emplace_back(active ? "audioSession.activate.refused" : "audioSession.deactivate.refused");
        error = "audio session refused";
        return false;
    }
    calls.emplace_back(active ? "audioSession.activate" : "audioSession.deactivate");
    return true;
}

int main()
{
    RunsAndStopsNormally();
    StopsFromInsideIterate();
    BackgroundStopsIterating();
    IterateFailureReportsSDLError();
    EventCanEndTheGame();
    InitFailureFinishesBeforeReturning();
    QuitFailuresBecomeTheResult();
    AbortedStartResetsWithoutCompletion();
    RefusedAudioSessionStillStarts();
    BackKeyReachesTheEngine();
    OneSessionAtATime();
    SecondSessionStartsClean();
    std::printf("PASS: SDL host start, step, stop, foreground, back key, failure, abort and reset\n");
    return 0;
}
