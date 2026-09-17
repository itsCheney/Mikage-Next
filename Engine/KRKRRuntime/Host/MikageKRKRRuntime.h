#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MikageKRKRStepResult {
    MIKAGE_KRKR_STEP_IDLE = 0,
    MIKAGE_KRKR_STEP_RUNNING = 1,
    MIKAGE_KRKR_STEP_FINISHED = 2,
    MIKAGE_KRKR_STEP_FAILED = 3
} MikageKRKRStepResult;

typedef void (*MikageKRKRMenuCallback)(void *context);
typedef void (*MikageKRKRCompletionCallback)(bool success, const char *message, void *context);
typedef void (*MikageKRKRLogCallback)(const char *source, int32_t level, const char *message);

// Strings are borrowed for the duration of the callback; copy before returning.
// May be called on runtime worker threads. NULL disables host log collection.
void MikageKRKRSetLogCallback(MikageKRKRLogCallback callback);

typedef struct MikageKRKRStats {
    double framesPerSecond;
    double frameTimeMilliseconds;
    int32_t drawableWidth;
    int32_t drawableHeight;
    char renderer[32];
} MikageKRKRStats;

bool MikageKRKRStart(const char *gamePath,
                     const char *renderer,
                     void *uiWindowScene,
                     bool menuGestureEnabled,
                     MikageKRKRMenuCallback menuCallback,
                     MikageKRKRCompletionCallback completionCallback,
                     void *context);
MikageKRKRStepResult MikageKRKRStep(void);
void MikageKRKRRequestStop(void);
bool MikageKRKRSetForeground(bool foreground);
bool MikageKRKRGetStats(MikageKRKRStats *stats);
bool MikageKRKRIsRunning(void);
const char *MikageKRKRLastError(void);
void *MikageKRKRNativeWindow(void);

#ifdef __cplusplus
}
#endif
