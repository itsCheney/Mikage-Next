# Audio pan

Plays a stereo tone through the production SDL3 sound buffer
(`environ/sdl3/sdl3_audio.cpp`) into SDL's disk audio driver, then checks
the left and right levels for several `pan` values, including a change while
the tone is already queued. Only the engine's critical section is replaced,
with a recursive mutex.

When SDL2 (or sdl2-compat) is found, `audio-pan-sdl2-tests` does the same for
the SDL2 sound buffer (`environ/sdl2/sdl2_audio.cpp`), with mono sources and
volume changes too. Under sdl2-compat it needs SDL3 on the library path.
