cmake_minimum_required(VERSION 3.20)
project(EmoteMetadataTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(READER "${CORE}/plugins/emoteplayer/emotefile.cpp")
set(GENERATED_READER "${CMAKE_CURRENT_BINARY_DIR}/ProductionReader.inc")
add_custom_command(OUTPUT "${GENERATED_READER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-production.py"
            --source "${READER}" --output "${GENERATED_READER}"
    DEPENDS "${READER}" "${CMAKE_CURRENT_LIST_DIR}/extract-production.py"
    VERBATIM)
file(GLOB TJS_SOURCES CONFIGURE_DEPENDS "${CORE}/tjs2/*.cpp")
add_executable(emote-metadata-tests EmoteMetadataTests.cpp "${GENERATED_READER}"
    "${CORE}/plugins/psbfile/PSBData.cpp" ${TJS_SOURCES})
target_include_directories(emote-metadata-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CORE}/tjs2" "${CORE}/environ" "${CORE}/plugins")
target_compile_definitions(emote-metadata-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1)
if(WIN32)
    target_compile_definitions(emote-metadata-tests PRIVATE _KRKRSDL3_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(emote-metadata-tests PRIVATE _KRKRSDL3_MACOS=1)
else()
    target_compile_definitions(emote-metadata-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
enable_testing()
add_test(NAME emote-metadata-tests COMMAND emote-metadata-tests)
