# ONSRuntime host framework

Mikage Next runs ONScripter games with [ONScripter Yuri](https://github.com/YuriSizuku/OnscripterYuri), built as an embeddable iOS dynamic framework.

Pinned inputs:

- `Source/` submodule: `76b3b0118566d25efa103b730e915f1a4a6642bc` (`itsCheney/OnscripterYuri`, branch `shared-sdl3/ons`, based on upstream `master`)
- sdl2-compat: `d02db3d9f09ef18f0adb05981ebd1760b9454bf1` (`itsCheney/sdl2-compat`, branch `shared-sdl3/ons`, based on `release-2.32.74`)
- FFmpeg 7.1.2 for movies: the static libraries `scripts/build-krkr-ios.sh` installs with vcpkg for KiriKiri (`build/krkr-vcpkg-installed`). `scripts/build-ons-ios.sh` installs the same vcpkg manifest itself when they are missing.
- [GeneralUser GS](https://github.com/mrbumpy409/GeneralUser-GS) v2.0.3 SoundFont for MIDI music, downloaded at commit `684543d5e5efaef08d02be50dcda8d552478fa60` and checked by SHA-256
- SDL_image `release-2.8.12`, SDL_mixer `release-2.8.2`, SDL_ttf `release-2.24.0` and Lua `v5.4.7`, pinned by commit in `Source/src/onsyuri_mikage/CMakeLists.txt`

ONScripter Yuri is an SDL2 program. It doesn't bring its own SDL: sdl2-compat is linked statically into `ONSRuntime.framework` and loads the app's shared `SDL3.framework`, so KiriKiri and ONScripter use one SDL. The sdl2-compat fork only adds an iOS loader path and allows the static build on iOS. `scripts/build-ons-ios.sh` builds the device and Apple Silicon simulator slices against `build/SDL3.xcframework`, checks that each exports the Mikage host API and no SDL code of its own, then creates `build/ONSRuntime.xcframework`. Build SDL3 first, then ONScripter.

The engine runs on its own stack on the main thread and hands control back to the app after each frame, so the app keeps its run loop. `App/Engine/ONSSession.swift` drives it with the shared Swift host in `App/Engine/SDLHost` (landscape, window switching, frame stepping, foreground changes). It doesn't compile in the native `Engine/MikageSDLHost`, which is built around SDL's main callbacks; ONScripter keeps its own loop and its own audio session and reset code in `MikageONSRuntime.mm`. The host API, the engine changes and the build are described in `Source/src/onsyuri_mikage/README.md`.

MIDI music plays through Apple's MIDI synthesizer with GeneralUser GS (about 31 MB), which the build copies into `ONSRuntime.framework` with its license. Movies are decoded with FFmpeg and drawn over the game; a tap skips them when the game allows it.

Games keep their aspect ratio with black bars. The Settings switch "Stretch 4:3 Games to 16:9" (off by default) widens a game narrower than 16:9 to 16:9 first. The game menu's Back tile presses Escape, which ONScripter treats as a right-click.

Known limits: system fonts use the first face of their collection.

ONScripter Yuri and ONScripter are GPL-2.0-or-later; sdl2-compat, SDL_image, SDL_mixer and SDL_ttf are zlib; Lua is MIT; FFmpeg is built LGPL-2.1-or-later; GeneralUser GS may be used and redistributed freely (its license is `GeneralUser-GS-LICENSE.txt` in the framework). The licenses are in the submodule and in the sources the build fetches.
