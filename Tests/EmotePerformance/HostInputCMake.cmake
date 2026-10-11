find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(HOST_INPUT_SOURCE "${EMOTE_CORE}/../host/MikageKRKRRuntime.mm")
set(HOST_INPUT_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/ProductionHostInput.inc")
add_custom_command(OUTPUT "${HOST_INPUT_GENERATED}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-host-input.py"
        --source "${HOST_INPUT_SOURCE}" --output "${HOST_INPUT_GENERATED}"
    DEPENDS "${HOST_INPUT_SOURCE}" "${CMAKE_CURRENT_LIST_DIR}/extract-host-input.py"
    VERBATIM)
add_executable(emote-host-input-tests "${CMAKE_CURRENT_LIST_DIR}/HostInputTests.cpp" "${HOST_INPUT_GENERATED}")
target_include_directories(emote-host-input-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME emote-host-input-tests COMMAND emote-host-input-tests)
