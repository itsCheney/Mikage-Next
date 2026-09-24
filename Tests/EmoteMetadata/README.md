# Emote metadata regressions

This harness compiles the production TJS VM, `PSBData.cpp`, and the exact reader
method bodies extracted from `emotefile.cpp` at build time. It runs without SDL,
Metal, GLM, or an installed game. No reader logic is copied into a test double.

```sh
cmake -S Tests/EmoteMetadata -B build/emote-metadata-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/emote-metadata-tests --parallel
ctest --test-dir build/emote-metadata-tests --output-on-failure
```

On Windows with Strawberry C++, specify an installed Python interpreter if
CMake does not find it automatically:

```powershell
cmake -S Tests/EmoteMetadata -B build/emote-metadata-tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=C:/path/to/python.exe
cmake --build build/emote-metadata-tests --parallel 6
ctest --test-dir build/emote-metadata-tests --output-on-failure
```

The synthetic PSB byte fixtures cover:

- Equality between full-root materialization followed by dictionary lookup and
  the targeted variable-frame lookup, including unknown nested fields.
- UTF-8 labels, duplicate labels, missing metadata/variables/labels/frame lists,
  and unloaded files. A first matching label with no frame list returns void,
  matching the production Dictionary `PropGet(0, ...)` behavior.
- New arrays and nested objects on each call, with mutation isolation between
  callers and subsequent queries.
- All integer widths, zero/float/double, strings, booleans, void/null,
  resource placeholders, packed arrays, and trailing void array elements.
  A separate array assembled through the old `Array.add` API is the reference
  for the primitive-list insertion semantics; signed packed-array conversion
  and empty array/dictionary identity are also checked.
- PSB versions 2, 3, and 4. The tracked stream throws if a metadata query reads
  even one byte in an unrelated 8,000-entry animation subtree. Read volume must
  also be at least 100 times smaller than full-root traversal.

Running the executable directly prints the measured read volumes and elapsed
times. Timing is informative only; there is no wall-clock pass threshold and
these synthetic desktop timings do not predict iOS frame rates.

Limitations: the fixture supplies the decoded name cache/string-offset table,
so name-trie decoding, file I/O, encrypted/compressed containers, and animation
tree generation are not exercised. PSB v1 uses a separate pre-existing reader
path and is outside these tests. The minimal class declaration exposes only
the fields used by the extracted methods; this is not a full application build.
The reference lookup follows script dictionary behavior but does not recreate
the old `_metadata->_varList.size()` scan bound. Renderer behavior, caches,
real-game compatibility, and Metal/iOS performance still need device testing.
