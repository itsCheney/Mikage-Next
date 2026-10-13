# KRKRRuntime host framework

Mikage Next builds KRKRSDL3 as an embeddable iOS dynamic framework instead of using its standalone SDL application entry point.

Pinned inputs:

- `krkrsdl3_build` fork submodule: `0c58847947a6d6f112f84f6b16b396741f047cf6` (`itsCheney/krkrsdl3_build`, branch `shared-sdl3`, based on `mikage-beta`)
- nested `krkrsdl3` core submodule: `d8d6ac99a4ca94a4e55f07a5781e07b2d756ebf8` (`itsCheney/krkrsdl3`, branch `mikage-beta`)
- vcpkg baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`
- shared SDL3 submodule `Engine/SDL3/Source`: `release-3.4.12` (`f87239e71e42da91ca317a12eefb82cfbf3393eb`), the same SDL release vcpkg provides at that baseline

`Source/` is the pinned `itsCheney/krkrsdl3_build` fork and contains the public C API, frame metrics, lifecycle driver, and its nested pinned `itsCheney/krkrsdl3` core fork. The forked changes make KRKR restart-safe, maintain drawable-space touch coordinates, isolate graphics/event/media/session state, invalidate late video frames, and append runtime logging to each game's `savedata/krkr.console.log`. `scripts/build-krkr-ios.sh` initializes the nested submodule, verifies the scenario cache isolation, builds device and Apple Silicon simulator frameworks, then creates `build/KRKRRuntime.xcframework`.

SDL3 is not compiled into KRKRRuntime. `scripts/build-sdl3-ios.sh` builds the pinned SDL submodule as a dynamic `build/SDL3.xcframework`, which the app embeds once so every engine runtime shares one SDL (two static copies would register duplicate SDL UIKit classes and keep separate hint, event and audio state). `build-krkr-ios.sh` passes each slice to the build fork as `KRKR_SDL3_FRAMEWORK`; KRKR still compiles against vcpkg's SDL3 headers, configure fails if their version differs from the framework's, and the script rejects a runtime that does not load `@rpath/SDL3.framework/SDL3` or still defines SDL code of its own. Build SDL3 first, then KRKR.

The host patch deliberately removes only `sdl3_entry.cpp` from the framework build. The official standalone iOS target remains unchanged when `KRKR_HOST_LIBRARY=OFF`.

The KRKRSDL3 license is reproduced in `KRKRSDL3-LICENSE.txt`. Distributors must review its source-availability condition before distributing modified KRKRSDL3 binaries with commercial game ports.

Ordinary GPU Layer composition is experimental and stays on app `beta` and core/build `mikage-beta` until device validation. Stable Metal presentation remains on app `main` and core/build `mikage`. See [implementation and validation notes](Source/docs/metal-layer-composition.md).
