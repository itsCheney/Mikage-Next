cmake_minimum_required(VERSION 3.20)
project(LayerInputTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(EXTRACTED
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionManagerMembers.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionWindowMembers.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionInputHitTest.inc"
    "${CMAKE_CURRENT_BINARY_DIR}/ProductionInputMethods.inc")
add_custom_command(OUTPUT ${EXTRACTED}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-production.py"
            --source "${CORE}" --output "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/extract-production.py"
            "${CORE}/core/render/LayerManager.cpp" "${CORE}/core/render/LayerManager.h"
            "${CORE}/core/render/DrawDevice.cpp" "${CORE}/core/main/TVPWindow.h"
            "${CORE}/plugins/DrawDeviceD3D/DrawDeviceD3D.cpp"
            "${CORE}/core/script/tjsNativeLayer.cpp"
    VERBATIM)
add_executable(layer-input-tests LayerInputTests.cpp ${EXTRACTED})
target_include_directories(layer-input-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CORE}/core/render")
target_include_directories(layer-input-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../EmotePerformance/NodeStubs")
enable_testing()
add_test(NAME layer-input-tests COMMAND layer-input-tests)
