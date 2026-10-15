# ArtemisRuntime host framework

Mikage Next runs Artemis games with [openArtemis](https://github.com/itsCheney/openartemis), a C++20 / SDL3 reimplementation of the Artemis engine, built as an embeddable iOS dynamic framework.

Pinned inputs:

- `Source/` submodule: `itsCheney/openartemis`, branch `mikage`, based on `main`
- SDL3: the app's shared `build/SDL3.xcframework`
- FFmpeg 7.1.2 for movies: the static libraries `scripts/build-krkr-ios.sh` installs with vcpkg for KiriKiri (`build/krkr-vcpkg-installed`). `scripts/build-artemis-ios.sh` installs that manifest itself when they are missing.
- freetype, libpng, libjpeg-turbo, libvorbis, libtheora and PhysicsFS from openArtemis's own vcpkg manifest (baseline `8e8dfb4b`, the same as KiriKiri), installed into `build/artemis-vcpkg-installed`. Its iOS triplets are copies of KiriKiri's, so the packages both use come from the same binary cache.

openArtemis is an SDL3 main-callback program, like KRKRSDL3. `Source/src/mikage` compiles its standalone host (`src/app/main.cpp`) into `ArtemisRuntime.framework` together with the shared `Engine/MikageSDLHost`, which drives the four SDL callbacks one frame per display refresh. `App/Engine/ArtemisSession.swift` runs it with the Swift host in `App/Engine/SDLHost`. `scripts/build-artemis-ios.sh` builds the device and Apple Silicon simulator slices, checks that each links the shared SDL3, carries no SDL code of its own and exports only the Mikage host API, then creates `build/ArtemisRuntime.xcframework`. Build SDL3 and KiriKiri first.

A game folder needs `root.pfs` (with its `.pfs.000` volumes), another `.pfs` pack, or an extracted project with `system.ini`. openArtemis boots the first of the `[ANDROID]`, `[IOS]` and `[WINDOWS]` sections `system.ini` has. Saves go to the game folder's `savedata`. The game menu's Back tile sends a right click, which Artemis games use to close menus and step back. A three-finger touch opens the Mikage menu when that setting is on.

Known limits: openArtemis's GPU path still reads some effects back to the CPU (group shaders, rule transitions, screen blends); see the openArtemis assessment in the project files.

openArtemis is MIT OR GPL-3.0-or-later and Mikage uses it under GPL-3.0-or-later; FFmpeg is built LGPL-2.1-or-later. The licenses are in the submodule.
