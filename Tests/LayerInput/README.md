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
