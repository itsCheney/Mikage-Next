cmake_minimum_required(VERSION 3.20)
project(LayerExSaveTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)
set(EXTRACTED "${CMAKE_CURRENT_BINARY_DIR}/ProductionPngEncoder.inc")
add_custom_command(OUTPUT "${EXTRACTED}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-encoder.py"
            --source "${CORE}" --output "${EXTRACTED}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/extract-encoder.py" "${CORE}/plugins/LayerExSave.cpp"
    VERBATIM)
add_executable(layerexsave-tests LayerExSaveTests.cpp "${EXTRACTED}")
target_include_directories(layerexsave-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(layerexsave-tests PRIVATE ZLIB::ZLIB)
enable_testing()
add_test(NAME layerexsave-tests COMMAND layerexsave-tests)
