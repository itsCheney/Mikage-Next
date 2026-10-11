cmake_minimum_required(VERSION 3.20)
project(PSBFileTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)
set(MEMORY_STREAM "${CMAKE_CURRENT_BINARY_DIR}/ProductionMemoryStream.inc")
add_custom_command(OUTPUT "${MEMORY_STREAM}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-memory-stream.py"
            --source "${CORE}/core/archive/UtilStreams.cpp" --output "${MEMORY_STREAM}"
    DEPENDS "${CORE}/core/archive/UtilStreams.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-memory-stream.py"
    VERBATIM)
file(GLOB TJS_SOURCES CONFIGURE_DEPENDS "${CORE}/tjs2/*.cpp")
add_executable(psb-file-tests PSBFileTests.cpp "${MEMORY_STREAM}"
    "${CORE}/plugins/psbfile/PSBFile.cpp" "${CORE}/plugins/psbfile/PSBData.cpp"
    "${CORE}/plugins/psbfile/PSBMedia.cpp" ${TJS_SOURCES})
include("${CMAKE_CURRENT_LIST_DIR}/../TJSBytecodeHash.cmake")
mikage_add_tjs_bytecode_hash(psb-file-tests "${CORE}")
target_include_directories(psb-file-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CORE}/tjs2" "${CORE}/environ" "${CORE}/plugins" "${CORE}/core/archive")
target_compile_definitions(psb-file-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1)
target_link_libraries(psb-file-tests PRIVATE ZLIB::ZLIB)
if(WIN32)
    target_compile_definitions(psb-file-tests PRIVATE _KRKRSDL3_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(psb-file-tests PRIVATE _KRKRSDL3_MACOS=1)
else()
    target_compile_definitions(psb-file-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
enable_testing()
add_test(NAME psb-file-tests COMMAND psb-file-tests)
