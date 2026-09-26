# Native Array/Dictionary construction regressions

Builds the real TJS VM and compares the opt-in direct class-member transfer
against the original virtual `EnumMembers` callback path.

```sh
cmake -S Tests/TJSNativeFactories -B build/tjs-native-factory-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/tjs-native-factory-tests --parallel
ctest --test-dir build/tjs-native-factory-tests --output-on-failure
```

On Windows, add `-G Ninja` when using Strawberry C++/Ninja.

The tests use actual `TJSCreateArrayObject`/`TJSCreateDictionaryObject` factories
and their exposed class objects to change constructors, instance members,
methods, hidden/static flags and already-bound closures. They verify native
class IDs and `IsInstanceOf`, Array count/length/add, Dictionary `assignStruct`
deep copies, independent mutations and unchanged finalization behavior.

A separate native-class probe compares exact write order and flags (including
colliding symbol buckets), `PropSetByVS` fallback, ignored setter failures,
propagated exceptions and name lifetime when a destination mutates the source
class during a fallback. Native classes that do not opt in still execute their
virtual enumeration, including members produced only by that override.

The executable prints timings for 20,000 create/release operations using the
same Array/Dictionary classes with the optimization toggled. These are useful
for local comparison only; timing does not determine test success and does not
predict device frame rates. The test does not render Emote animation or test
arbitrary third-party plugins.
