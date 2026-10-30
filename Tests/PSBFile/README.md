# Production PSB scene lifecycle

This fixture compiles the real PSBFile/PSBData/PSBMedia translation units and
the complete TJS VM. Input storage is doubled; memory-stream implementation is
extracted unchanged from production UtilStreams.cpp. Synthetic PSB/MDF assets
come from the shared PSBFixture writer. No proprietary game assets are needed.

```sh
cmake -S Tests/PSBFile -B build/psb-file-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/psb-file-tests --parallel
ctest --test-dir build/psb-file-tests --output-on-failure
```

Tests compare diagnostic on/off content, stream calls and ownership, script
finalizer order, plain/MDF parsing and null/open/read/short/decode/version failure
paths. Native owner deletion must not free the root tree: its containers are
released over later frames, and the test runs those frames itself (the frame
loop is the only event double). A marker advances the test clock when it is
deleted, so the destroy scope staying under that cost proves the tree left it.

A separate retained-root test keeps the root alive after deleting its PSB owner.
Its final external release destroys the tree outside psb.destroy, after the
deferred release has dropped the PSBFile's own references.

Production VM register blocks retain temporary variants until compaction. The
fixture explicitly compacts after clearing script references, so the last owner
release and root-tree release are actually reached before asserting counters.
This does not add compaction to production code. Forced global TJS teardown can
bypass native Destruct hooks and is outside this ordinary owner-release test.

CPU output is v2, retaining the v1 parser. Inclusive/residual wall time is not
pure VM CPU; instrumented child phases must not be added to their parent load.
