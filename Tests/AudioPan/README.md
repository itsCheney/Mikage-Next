# Audio pan

Plays a stereo tone through the production SDL3 sound buffer
(`environ/sdl3/sdl3_audio.cpp`) into SDL's disk audio driver, then checks
the left and right levels for several `pan` values, including a change while
the tone is already queued. Only the engine's critical section is replaced,
with a recursive mutex.
