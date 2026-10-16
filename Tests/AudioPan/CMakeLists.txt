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
