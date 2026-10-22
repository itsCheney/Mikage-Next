# Shared SDL host lifecycle tests

These tests run the production `Engine/MikageSDLHost/MikageSDLHost.cpp` against
a scripted SDL3 main-callback engine, with real SDL running headless. Only the
platform audio session is replaced, by a recorder that can also refuse. They
cover start and init failure, the event pump and iterate order, stop requests,
background and foreground changes (including a refused audio session),
failures and exceptions while quitting, an aborted start, and a second session
starting cleanly after the first.

The KRKR bridge on top of the host, and the UIKit window and AVAudioSession
calls, still need the iOS framework build in CI.

```sh
cmake -S Tests/SDLHost -B build/sdl-host-tests -DCMAKE_PREFIX_PATH="$(brew --prefix sdl3)"
cmake --build build/sdl-host-tests --parallel
ctest --test-dir build/sdl-host-tests --output-on-failure
```
