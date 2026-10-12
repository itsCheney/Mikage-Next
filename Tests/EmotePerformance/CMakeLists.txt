cmake_minimum_required(VERSION 3.20)
project(EmotePerformanceTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
get_filename_component(EMOTE_CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
include(NodeCMake.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CAPTURE_EXCHANGE "${CMAKE_CURRENT_BINARY_DIR}/ProductionCaptureExchange.inc")
add_custom_command(OUTPUT "${CAPTURE_EXCHANGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-capture-exchange.py"
            --core "${EMOTE_CORE}" --output "${CAPTURE_EXCHANGE}"
    DEPENDS "${EMOTE_CORE}/plugins/emoteplayer/emoteplayerclass.h"
            "${EMOTE_CORE}/plugins/emoteplayer/emoteplayerclass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/extract-capture-exchange.py"
    VERBATIM)
add_executable(emote-capture-exchange-tests CaptureExchangeTests.cpp "${CAPTURE_EXCHANGE}")
target_include_directories(emote-capture-exchange-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME emote-capture-exchange-tests COMMAND emote-capture-exchange-tests)
