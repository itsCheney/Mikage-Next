# RenPyRuntime host framework

Mikage Next runs Ren'Py games with Ren'Py 8 itself, built for iOS as an embeddable dynamic framework.

Pinned inputs:

- `Source/` submodule: `itsCheney/renpy`, branch `mikage`, based on Ren'Py `master` (8.6, the first Ren'Py on SDL3). Everything Mikage adds is in `Source/mikage` (see its README); the only other change is `src/renpygl.h`, which takes OpenGL ES types from MetalANGLE.
- CPython 3.12.8, downloaded and checksummed by `scripts/build-renpy-ios.sh`, cross-built for iOS the way renpy-build does it (patches in `Source/mikage/python`). Static, with the standard library's C modules built in; no `ssl`.
- Cython 3.3.0 and pyobjus (kivy/pyobjus `b61d65f`, renpy-build's commit) on the host, to generate C for Ren'Py's modules and pyobjus.
- SDL3: the app's shared `build/SDL3.xcframework`. Its OpenGL ES windows run on MetalANGLE (`scripts/fetch-metalangle-ios.sh`, the binary renpy-build ships, BSD licensed; see `Engine/MetalANGLE/LICENSE`), never on Apple's deprecated OpenGLES framework.
- FFmpeg: the static libraries `scripts/build-krkr-ios.sh` installs for KiriKiri.
- SDL3_image, FreeType, HarfBuzz, FriBidi, libpng, libjpeg-turbo, libwebp, assimp, libffi, zlib, bzip2 and liblzma from `Source/mikage/vcpkg.json` (baseline `8e8dfb4b`, KiriKiri's triplets), installed into `build/renpy-vcpkg-installed`.

`scripts/build-renpy-ios.sh` generates Ren'Py's C (`setup.py generate`), builds CPython and the framework for the device and the Apple Silicon simulator, checks that each links the shared SDL3 and MetalANGLE (and not OpenGLES) and exports only the Mikage host API, copies the Python standard library (`base/lib/python312.zip`), Ren'Py's Python and `renpy/common` into the framework's `base/`, then creates `build/RenPyRuntime.xcframework`. Fetch MetalANGLE and build SDL3 first; it needs a host Python 3.12 (`RENPY_HOST_PYTHON`, or `python3.12` on PATH).

Ren'Py keeps its own blocking main loop. The runtime runs it on a separate stack on the main thread and resumes it once per display refresh from `App/Engine/RenPySession.swift`; waiting for events, sleeping and presenting a frame hand control back to the app. A game folder is the one holding `game/` (with `.rpy`, `.rpyc` or `.rpa` files); the desktop build's `renpy/` and `lib/` folders are ignored. Saves go to the game folder's `savedata`. The menu's Back tile sends Escape (Ren'Py's game menu); leaving the game quits it without Ren'Py's confirmation.

Known limits: one Ren'Py game per app launch (Python can't restart inside a process; the app says so), no Live2D, no screenshots from the Mikage menu, and `renpy/common` is compiled when a game starts because the framework is read-only.

Ren'Py is MIT licensed with some LGPL components, CPython is under the PSF License, pyobjus is MIT, and FFmpeg is built LGPL-2.1-or-later. The licenses are in the submodule and the downloaded sources.
