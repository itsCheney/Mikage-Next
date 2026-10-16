cmake_minimum_required(VERSION 3.20)
project(AudioPanTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
find_package(SDL3 REQUIRED CONFIG)
# The production SDL3 sound buffer, played through SDL's disk audio driver.
add_executable(audio-pan-tests AudioPanTests.cpp "${CORE}/environ/sdl3/sdl3_audio.cpp")
target_include_directories(audio-pan-tests PRIVATE
    "${CORE}/tjs2" "${CORE}/environ" "${CORE}/core/base" "${CORE}/core/utils"
    "${CORE}/core/media/sound" "${CORE}/core/main" "${CORE}/core/msg")
target_link_libraries(audio-pan-tests PRIVATE SDL3::SDL3)
if(APPLE)
    target_compile_definitions(audio-pan-tests PRIVATE _KRKRSDL3_MACOS=1)
else()
    target_compile_definitions(audio-pan-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
enable_testing()
add_test(NAME audio-pan-tests COMMAND audio-pan-tests "${CMAKE_CURRENT_BINARY_DIR}")

# The production SDL2 sound buffer too, when SDL2 (or sdl2-compat) is there.
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    add_executable(audio-pan-sdl2-tests AudioPanSDL2Tests.cpp "${CORE}/environ/sdl2/sdl2_audio.cpp")
    target_include_directories(audio-pan-sdl2-tests PRIVATE
        "${CORE}/tjs2" "${CORE}/environ" "${CORE}/core/base" "${CORE}/core/utils"
        "${CORE}/core/media/sound" "${CORE}/core/main" "${CORE}/core/msg")
    target_link_libraries(audio-pan-sdl2-tests PRIVATE SDL2::SDL2)
    if(APPLE)
        target_compile_definitions(audio-pan-sdl2-tests PRIVATE _KRKRSDL3_MACOS=1)
    else()
        target_compile_definitions(audio-pan-sdl2-tests PRIVATE _KRKRSDL3_LINUX=1)
    endif()
    add_test(NAME audio-pan-sdl2-tests COMMAND audio-pan-sdl2-tests "${CMAKE_CURRENT_BINARY_DIR}")
endif()
