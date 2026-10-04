# Layer input regression tests

These portable tests extract unchanged method bodies from the production layer
manager and built-in draw devices. They cover property-only cursor/hint lookup,
capture and reentrant/throwing callbacks, normal mouse/touch dispatch, and draw
device attachment/detachment. The production default flag, setter, owner getter,
window capability, and point-read trace header are used directly.

The harness replaces graphics resources, leaf-layer traversal/events, TJS object
references, and window/owner interfaces with observable stubs. Device constructors
only initialize stub state; the tested add/remove, window change, capability
selection and destructor bodies are production code. This does not compile full
draw-device class layouts or render Metal pixels. The full iOS framework build
in CI remains necessary to validate native headers and integration.

```sh
cmake -S Tests/LayerInput -B build/layer-input-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/layer-input-tests --parallel
ctest --test-dir build/layer-input-tests --output-on-failure
```

Checks use exceptions rather than `assert`, and therefore stay active in Release.
# Async Emote alpha input

The same extracted production manager methods now cover the opt-in async mask
path. `LayerManager` preflight, FIFO replay, displayed-frame binding, native
mask-hit reads and pointer entry gates compile directly from core sources.
Fixtures provide only owner lifetime, window callbacks and controllable GPU
completion/presentation tickets.

Cases include cold cache, GPU completion before presentation, one frame shared
by all candidates, resize/COW coordinate binding, later-version writes, pointer
chain cancellation, owner replacement, failed drawable plus removed layer,
128-event backpressure and preservation of explicit immediate script queries.
`Tests/EmotePerformance` separately compiles the production alpha tile cache and
checks every alpha byte, tile edges, staging lease release, cancellation and
cache budgets. Native Metal backend read/presentation tests run in Apple CI.

Budget liveness uses two simultaneous read allocations with three changing
candidate layers and a new event arriving before the first finishes. The test
retains F's source epochs, supplements missing reads in later normal frames,
checks FIFO priority, and verifies F's alpha/mapping plus complete lease/ref
release. It does not raise the budget or restart all candidates every frame.
