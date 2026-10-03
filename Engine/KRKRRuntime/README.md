# KRKRRuntime host framework

Mikage Next builds KRKRSDL3 as an embeddable iOS dynamic framework instead of using its standalone SDL application entry point.

Pinned inputs:

- `krkrsdl3_build` fork submodule: `13abe820613fbb24196cb8936416dc75d0b872bb` (`itsCheney/krkrsdl3_build`, branch `codex/metal-free-build`)
- nested `krkrsdl3` core submodule: `3aa46b58c54281eb11454e23633411b5ebf10d75` (`itsCheney/krkrsdl3`, branch `codex/metal-free-core`)
- vcpkg baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`

`Source/` is the pinned `itsCheney/krkrsdl3_build` fork and contains the public C API, frame metrics, lifecycle driver, and its nested pinned `itsCheney/krkrsdl3` core fork. The forked changes make KRKR restart-safe, maintain drawable-space touch coordinates, isolate graphics/event/media/session state, invalidate late video frames, and append runtime logging to each game's `savedata/krkr.console.log`. `scripts/build-krkr-ios.sh` initializes the nested submodule, verifies the scenario cache isolation, builds device and Apple Silicon simulator frameworks, then creates `build/KRKRRuntime.xcframework`.

The host patch deliberately removes only `sdl3_entry.cpp` from the framework build. The official standalone iOS target remains unchanged when `KRKR_HOST_LIBRARY=OFF`.

The KRKRSDL3 license is reproduced in `KRKRSDL3-LICENSE.txt`. Distributors must review its source-availability condition before distributing modified KRKRSDL3 binaries with commercial game ports.

The app, build fork, and core fork keep their native Metal experiments on `metal_dev`. The current iOS runtime uses OpenGL ES or software composition.
