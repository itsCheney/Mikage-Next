# KRKRRuntime host framework

Mikage Next builds KRKRSDL3 as an embeddable iOS dynamic framework instead of using its standalone SDL application entry point.

Pinned inputs:

- `krkrsdl3_build`: `66fb7d9533478d33317208cd8ec8696ab9340d6f`
- `krkrsdl3` core submodule: `5a8bd422f82d3758045f403520a64b772a59f40c`
- vcpkg baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`

`Host/` contains the public C API and frame driver. `Patches/` contains the complete corresponding source changes applied to upstream. `scripts/build-krkr-ios.sh` fetches the pinned sources, applies these files, builds device and Apple Silicon simulator frameworks, then creates `build/KRKRRuntime.xcframework`.

The host patch deliberately removes only `sdl3_entry.cpp` from the framework build. The official standalone iOS target remains unchanged when `KRKR_HOST_LIBRARY=OFF`.

The KRKRSDL3 license is reproduced in `KRKRSDL3-LICENSE.txt`. Distributors must review its source-availability condition before distributing modified KRKRSDL3 binaries with commercial game ports.
