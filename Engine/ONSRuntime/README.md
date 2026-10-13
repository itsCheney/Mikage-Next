# ONSRuntime host framework

Mikage Next runs ONScripter games with [ONScripter Yuri](https://github.com/YuriSizuku/OnscripterYuri), built as an embeddable iOS dynamic framework.

Pinned inputs:

- `Source/` submodule: `365f1e2b050969cd1d8008e7474de22bc1bf844d` (`itsCheney/OnscripterYuri`, branch `shared-sdl3/ons`, based on upstream `master`)
- sdl2-compat: `7f8f1c7293decab05c59aa28a2c9b095546efa29` (`itsCheney/sdl2-compat`, branch `shared-sdl3/ons`, based on `release-2.32.74`)
- SDL_image `release-2.8.12`, SDL_mixer `release-2.8.2`, SDL_ttf `release-2.24.0` and Lua `v5.4.7`, pinned by commit in `Source/src/onsyuri_mikage/CMakeLists.txt`

ONScripter Yuri is an SDL2 program. It doesn't bring its own SDL: sdl2-compat is linked statically into `ONSRuntime.framework` and loads the app's shared `SDL3.framework`, so KiriKiri and ONScripter use one SDL. The sdl2-compat fork only adds an iOS loader path and allows the static build on iOS. `scripts/build-ons-ios.sh` builds the device and Apple Silicon simulator slices against `build/SDL3.xcframework`, checks that each exports the Mikage host API and no SDL code of its own, then creates `build/ONSRuntime.xcframework`. Build SDL3 first, then ONScripter.

The engine runs on its own stack on the main thread and hands control back to the app after each frame, so the app keeps its run loop. `App/Engine/ONSSession.swift` drives it from a display link. The host API, the engine changes and the build are described in `Source/src/onsyuri_mikage/README.md`.

Known limits: MIDI music and movies aren't played yet, and system fonts use the first face of their collection.

ONScripter Yuri and ONScripter are GPL-2.0-or-later; sdl2-compat, SDL_image, SDL_mixer and SDL_ttf are zlib; Lua is MIT. The licenses are in the submodule and in the sources the build fetches.
