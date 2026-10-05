cmake_minimum_required(VERSION 3.20)
project(TJSShutdownTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
get_filename_component(CORE "${CORE}" REALPATH)
file(GLOB TJS_SOURCES CONFIGURE_DEPENDS "${CORE}/tjs2/*.cpp")
# Included by the regression fixture to access the internal inline allocator.
list(REMOVE_ITEM TJS_SOURCES "${CORE}/tjs2/tjsInterCodeExec.cpp")
set(XP3_SESSION "${CMAKE_CURRENT_BINARY_DIR}/ProductionXP3Session.inc")
add_custom_command(OUTPUT "${XP3_SESSION}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-xp3-session.py"
            --source "${CORE}/plugins/xp3filter.cpp" --output "${XP3_SESSION}"
    DEPENDS "${CORE}/plugins/xp3filter.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-xp3-session.py"
    VERBATIM)
add_executable(tjs-shutdown-tests TJSShutdownTests.cpp ${TJS_SOURCES} "${XP3_SESSION}")
target_include_directories(tjs-shutdown-tests PRIVATE "${CORE}/tjs2" "${CORE}/environ" "${CMAKE_CURRENT_BINARY_DIR}")
# The shutdown regression needs no regular-expression engine/third-party deps.
target_compile_definitions(tjs-shutdown-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1)
if(WIN32)
    target_compile_definitions(tjs-shutdown-tests PRIVATE _KRKRSDL3_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(tjs-shutdown-tests PRIVATE _KRKRSDL3_MACOS=1)
else()
    target_compile_definitions(tjs-shutdown-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
enable_testing()
add_test(NAME tjs-shutdown-tests COMMAND tjs-shutdown-tests)
