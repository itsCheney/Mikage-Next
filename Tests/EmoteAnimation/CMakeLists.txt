cmake_minimum_required(VERSION 3.20)
project(EmoteAnimationTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
add_executable(emote-animation-tests EmoteAnimationTests.cpp
    "${CORE}/plugins/emoteplayer/emoteanimation.cpp")
target_include_directories(emote-animation-tests PRIVATE "${CORE}/plugins/emoteplayer")
if(MSVC)
    target_compile_options(emote-animation-tests PRIVATE /W4)
else()
    target_compile_options(emote-animation-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME emote-animation-tests COMMAND emote-animation-tests)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/ProductionIntegration.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionEngineDeclaration.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionRegistration.inc")
list(APPEND GENERATED "${CMAKE_CURRENT_BINARY_DIR}/ProductionClockMembers.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionD3D.inc" "${CMAKE_CURRENT_BINARY_DIR}/ProductionD3DRegistration.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionParameter.inc")
add_custom_command(OUTPUT ${GENERATED}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-integration.py"
        --core "${CORE}" --output "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS extract-integration.py "${CORE}/plugins/emoteplayer/emoterunner.cpp"
        "${CORE}/plugins/emoteplayer/emoterunner.h" "${CORE}/plugins/emoteplayer/emoteplayerclass.cpp"
        "${CORE}/plugins/emoteplayer/emoteplayer.cpp" "${CORE}/plugins/emoteplayer/emoteplayerclass.h"
        "${CORE}/plugins/emoteplayer/emotefile.cpp" "${CORE}/plugins/emoteplayer/emotefile.h"
        "${CORE}/plugins/DrawDeviceD3D/D3DEmotePlayer.cpp" "${CORE}/plugins/DrawDeviceD3D/DrawDeviceD3D_reg.cpp" VERBATIM)
file(GLOB TJS_SOURCES CONFIGURE_DEPENDS "${CORE}/tjs2/*.cpp")
add_executable(emote-integration-tests EmoteIntegrationTests.cpp ${GENERATED}
    "${CORE}/plugins/emoteplayer/emoteanimation.cpp" "${CORE}/plugins/ncbind/ncbind.cpp" ${TJS_SOURCES})
target_include_directories(emote-integration-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CORE}/plugins/emoteplayer" "${CORE}/plugins" "${CORE}/core/script"
    "${CORE}/core/msg" "${CORE}/tjs2" "${CORE}/environ")
target_compile_definitions(emote-integration-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1)
if(WIN32)
    target_compile_definitions(emote-integration-tests PRIVATE _KRKRSDL3_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(emote-integration-tests PRIVATE _KRKRSDL3_MACOS=1)
else()
    target_compile_definitions(emote-integration-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
add_test(NAME emote-integration-tests COMMAND emote-integration-tests)
add_test(NAME emote-animation-trajectories COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/check-trajectories.py" --executable "$<TARGET_FILE:emote-animation-tests>")
set(HOST_MODE_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/ProductionHostMode.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionModeConstructor.inc")
add_custom_command(OUTPUT ${HOST_MODE_GENERATED}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-host-mode.py"
        --core "${CORE}" --output "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS extract-host-mode.py "${CORE}/../host/MikageKRKRRuntime.mm"
        "${CORE}/plugins/emoteplayer/emoterunner.cpp" VERBATIM)
add_executable(emote-host-mode-tests HostEmoteModeTests.cpp ${HOST_MODE_GENERATED})
target_include_directories(emote-host-mode-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CORE}/../host"
    "${CORE}/plugins/emoteplayer" "${CMAKE_CURRENT_LIST_DIR}/../EmotePerformance/NodeStubs")
add_test(NAME emote-host-mode-tests COMMAND emote-host-mode-tests)
add_executable(emote-host-simulator-mode-tests HostEmoteModeTests.cpp ${HOST_MODE_GENERATED})
target_include_directories(emote-host-simulator-mode-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CORE}/../host"
    "${CORE}/plugins/emoteplayer" "${CMAKE_CURRENT_LIST_DIR}/../EmotePerformance/NodeStubs")
target_compile_definitions(emote-host-simulator-mode-tests PRIVATE TARGET_OS_SIMULATOR=1)
add_test(NAME emote-host-simulator-mode-tests COMMAND emote-host-simulator-mode-tests)
