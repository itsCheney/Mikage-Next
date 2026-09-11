# KRKRRuntime host framework

Mikage Next builds KRKRSDL3 as an embeddable iOS dynamic framework instead of using its standalone SDL application entry point.

Pinned inputs:

- `krkrsdl3_build`: `66fb7d9533478d33317208cd8ec8696ab9340d6f`
- `krkrsdl3` core submodule: `5a8bd422f82d3758045f403520a64b772a59f40c`
- vcpkg baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`

`Host/` contains the public C API, frame metrics and lifecycle driver. `Patches/krkrsdl3-core-host.patch` contains the embedding changes; `Patches/krkrsdl3-lifecycle-host.patch` contains restart-safe TJS cleanup, foreground audio/video suspension and Retina drawable changes. Follow-up patches serialize and reset the graphics cache, keep touch input in drawable coordinates until KRKR performs its letterbox transform, discard session-scoped event hooks, invalidate late video frames by session generation, and synchronously release cached video/audio players before SDL shutdown. Runtime logging is appended to each game's `savedata/krkr.console.log`. `scripts/build-krkr-ios.sh` fetches the pinned sources, applies the complete patch chain, builds device and Apple Silicon simulator frameworks, then creates `build/KRKRRuntime.xcframework`.

The host patch deliberately removes only `sdl3_entry.cpp` from the framework build. The official standalone iOS target remains unchanged when `KRKR_HOST_LIBRARY=OFF`.

The KRKRSDL3 license is reproduced in `KRKRSDL3-LICENSE.txt`. Distributors must review its source-availability condition before distributing modified KRKRSDL3 binaries with commercial game ports.
