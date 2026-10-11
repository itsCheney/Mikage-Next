get_filename_component(INPUT_CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
add_executable(emote-alpha-tile-tests InputTests.cpp)
target_compile_features(emote-alpha-tile-tests PRIVATE cxx_std_17)
target_include_directories(emote-alpha-tile-tests PRIVATE "${INPUT_CORE}/core/render")
add_test(NAME emote-alpha-tile-tests COMMAND emote-alpha-tile-tests)
# The existing LayerInput harness also extracts production preflight, queue,
# native mask-hit and pointer-entry methods, with the optimization on and off.
