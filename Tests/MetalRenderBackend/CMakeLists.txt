cmake_minimum_required(VERSION 3.20)
project(MetalRenderBackendTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
add_executable(metal-render-tests
    LayerUploadTests.cpp
    MetalRenderBackendTests.cpp
    "${CORE}/core/render/backend/SWRenderBackend.cpp")
target_include_directories(metal-render-tests PRIVATE "${CORE}/core/render" "${CORE}/environ")
if(NOT APPLE)
    add_library(c3-native-test-syntax OBJECT NativeLayerUploadSyntax.cpp)
    target_include_directories(c3-native-test-syntax PRIVATE "${CORE}/core/render" "${CORE}/environ")
endif()
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set_target_properties(metal-render-tests PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    find_package(SDL3 CONFIG REQUIRED)
    target_sources(metal-render-tests PRIVATE "${CORE}/core/render/backend/MetalRenderBackend.mm")
    set_source_files_properties("${CORE}/core/render/backend/MetalRenderBackend.mm"
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(metal-render-tests PRIVATE TEST_NATIVE_METAL=1)
    target_link_libraries(metal-render-tests PRIVATE SDL3::SDL3
        "-framework Foundation" "-framework Metal" "-framework QuartzCore")
endif()
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(UPLOAD_REPORT "${CMAKE_CURRENT_BINARY_DIR}/ProductionUploadReport.inc")
add_custom_command(OUTPUT "${UPLOAD_REPORT}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-upload-report.py"
        --source "${CORE}/core/render/backend/MetalRenderBackend.mm" --output "${UPLOAD_REPORT}"
    DEPENDS "${CORE}/core/render/backend/MetalRenderBackend.mm" "${CMAKE_CURRENT_LIST_DIR}/extract-upload-report.py"
    VERBATIM)
add_executable(c3-upload-emitter LayerUploadEmitter.cpp "${UPLOAD_REPORT}")
target_include_directories(c3-upload-emitter PRIVATE "${CORE}/core/render" "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME c3-upload-parser COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-c3-uploads.py")
set_tests_properties(c3-upload-parser PROPERTIES ENVIRONMENT "C3_UPLOAD_EMITTER=$<TARGET_FILE:c3-upload-emitter>")
add_test(NAME metal-render-tests COMMAND metal-render-tests)
set_tests_properties(metal-render-tests PROPERTIES SKIP_RETURN_CODE 77)

if(APPLE)
    set_tests_properties(metal-render-tests PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME metal-render-compute-tests COMMAND metal-render-tests)
    set_tests_properties(metal-render-compute-tests PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MIKAGE_METAL_LAYER_TILE_RENDERER=0")
endif()
