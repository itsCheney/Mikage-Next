cmake_minimum_required(VERSION 3.20)
project(FontFallbackTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CORE)
    set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
file(GLOB TJS_SOURCES CONFIGURE_DEPENDS "${CORE}/tjs2/*.cpp")
add_executable(font-fallback-tests FontFallbackTests.cpp ${TJS_SOURCES}
    "${CORE}/core/media/font/TVPFont.cpp"
    "${CORE}/core/media/font/FreeTypeImp.cpp"
    "${CORE}/core/media/font/FreeTypeFontRasterizer.cpp"
    "${CORE}/core/media/font/CharacterData.cpp")
foreach(dir archive base main media/font media/image msg render render/gl script utils utils/math)
    target_include_directories(font-fallback-tests PRIVATE "${CORE}/core/${dir}")
endforeach()
target_include_directories(font-fallback-tests PRIVATE "${CORE}/tjs2" "${CORE}/environ" "${CORE}/plugins"
    "${CMAKE_CURRENT_LIST_DIR}/../EmotePerformance/NodeStubs")
target_compile_definitions(font-fallback-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1 _KRKRSDL3_LINUX=1)
target_link_libraries(font-fallback-tests PRIVATE PkgConfig::FREETYPE)
include("${CMAKE_CURRENT_LIST_DIR}/../TJSBytecodeHash.cmake")
mikage_add_tjs_bytecode_hash(font-fallback-tests "${CORE}")
enable_testing()
add_test(NAME font-fallback-tests COMMAND font-fallback-tests)
set_tests_properties(font-fallback-tests PROPERTIES SKIP_RETURN_CODE 77)
