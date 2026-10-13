# KRKRRuntime host framework

Mikage Next builds KRKRSDL3 as an embeddable iOS dynamic framework instead of using its standalone SDL application entry point.

Pinned inputs:

- `krkrsdl3_build` fork submodule: `743d89b04ce85191f21922016a0cc1ea31c728df` (`itsCheney/krkrsdl3_build`, branch `mikage`)
- nested `krkrsdl3` core submodule: `7c50eb2f56199fd667e5243ea43f972fa6d2c7ee` (`itsCheney/krkrsdl3`, branch `mikage`)
- vcpkg baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`

`Source/` is the pinned `itsCheney/krkrsdl3_build` fork and contains the public C API, frame metrics, lifecycle driver, and its nested pinned `itsCheney/krkrsdl3` core fork. The forked changes make KRKR restart-safe, maintain drawable-space touch coordinates, isolate graphics/event/media/session state, invalidate late video frames, and append runtime logging to each game's `savedata/krkr.console.log`. `scripts/build-krkr-ios.sh` initializes the nested submodule, verifies the scenario cache isolation, builds device and Apple Silicon simulator frameworks, then creates `build/KRKRRuntime.xcframework`.

The host patch deliberately removes only `sdl3_entry.cpp` from the framework build. The official standalone iOS target remains unchanged when `KRKR_HOST_LIBRARY=OFF`.

The KRKRSDL3 license is reproduced in `KRKRSDL3-LICENSE.txt`. Distributors must review its source-availability condition before distributing modified KRKRSDL3 binaries with commercial game ports.
