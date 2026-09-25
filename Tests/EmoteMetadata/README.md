# Emote metadata regressions

These harnesses compile the production TJS VM, `PSBData.cpp`, and the exact
reader/loader/cache method bodies extracted from `emotefile.cpp` at build time.
They require a C++17 compiler, Python 3 and zlib development files, and run
without SDL, Metal, GLM, or an installed game. No reader/cache logic is copied
into a test double.

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

The metadata-only synthetic PSB byte fixtures cover:

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

The separate `emote-shared-loader-tests` executable exercises the actual
production `emotefile::load`, decoded-resource snapshots, shared LRU wrapper,
read-only streams, and full PSB name-trie/string/chunk-table decoding. It checks:

- Cold and warm loads across distinct `emotefile` instances, canonical aliases,
  and PSB v2/v3/v4; the second archive-member load must not reopen storage.
- Raw PSB, real zlib-compressed MDF, and an LZ4 frame with an uncompressed block,
  through the production container/decrypt helpers.
- Actual seed-encrypted PSB decoding, distinct seed cache keys, and real TJS
  custom decrypt callbacks that modify bytes; callbacks must bypass the cache
  and must not pollute an already cached version.
- Independent stream cursors and decoded tables; stream writes/truncation are
  rejected, invalid seeks retain the cursor, and EOF reads return the right size.
- Per-file runtime-tree generation, independent mutable markers, same-file reload
  state reset, and resource lifetime after clearing the cache or dropping players.
- No publication after decompression/tree failure or a reset during loading;
  explicit member/container invalidation preserves unrelated archives.
- Loose files bypass retention, reopen on each load, and see changed contents
  while existing players keep their original decoded data.
- Active XP3 content/extraction filters bypass shared hits so dynamic output and
  per-open callbacks are not skipped. The standalone cache script compiles the
  actual filter setters/query and verifies invalidation on install/remove.

Limitations: metadata-only fixtures inject their decoded tables; the loader
fixtures cover real table decoding but replace host storage with an in-memory
file map. Their `GenerateAniTree` allocates a small mutable marker in place of
the rendering/physics tree; this checks that every load regenerates independent
runtime state, not the real animation implementation. Decrypt callbacks use a
minimal buffer accessor around a real TJS closure. Session reset/invalidation
are called directly; application hook wiring and ResourceManager behavior are
covered separately by the resource-cache scripts. PSB v1 is outside these tests.
The metadata reference lookup does not recreate the old `_metadata->_varList.size()`
scan bound. Real-game compatibility and Metal/iOS performance still need device
testing; this is not a full application build.
