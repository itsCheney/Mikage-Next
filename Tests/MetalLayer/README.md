# Ordinary Layer rendering and overwrite tests

Build with CMake and run CTest. Apple hosts link the production Metal backend;
non-Apple hosts use a synchronous device double for GPU resources and the
production software renderer for the reference operations. Passing the portable
tests does not measure GPU performance or validate native Metal shaders.

`BitmapOverwriteTests.cpp` compiles the real bitmap declaration and extracted
production copy, adoption and copy-on-write methods. Only font initialization
is stubbed. The layer copy wrappers use their production bodies with a small
facade in place of the script/window machinery. Coverage includes:

- exclusive, shared and static GPU images, snapshot isolation and reference counts;
- no old-image Layer copy, CPU upload or readback on a successful GPU overwrite;
- rejected copies retaining the original image and releasing temporary textures;
- pinned CPU pointers and active read/write leases rejecting GPU overwrite;
- actual image dimensions, clipped CPU fallback, padded source rows and preservation
  of pixels outside a partial copy.

The full iOS framework build remains necessary to validate plugin and script
class integration. Test checks remain active in Release builds.

```sh
cmake -S Tests/MetalLayer -B build/metal-layer-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-layer-tests --parallel
ctest --test-dir build/metal-layer-tests --output-on-failure
```
