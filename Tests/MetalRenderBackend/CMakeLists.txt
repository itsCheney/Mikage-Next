cmake_minimum_required(VERSION 3.20)
project(MetalRenderBackendTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
add_executable(metal-render-tests
    LayerUploadTests.cpp
    LayerInitializationTests.cpp
    PendingFillTests.cpp
    OrdinaryLayerPipelineTests.cpp
    BinderDrawTests.cpp
    CPUFrameTests.cpp
    BoundedStackTests.cpp
    ImagePrefetchTests.cpp
    ImageAsyncTests.cpp
    ImageHolderTests.cpp
    StaticPresentationTests.cpp
    StaticPresentationHarnessTests.cpp
    TransitionCacheTests.cpp
    MetalRenderBackendTests.cpp
    "${CORE}/core/render/backend/SWRenderBackend.cpp")
# emotegeometry.h (the CPU reference for GPU mesh deformation) needs glm.
find_path(METAL_TEST_GLM_INCLUDE_DIR glm/glm.hpp)
if(NOT METAL_TEST_GLM_INCLUDE_DIR)
    include(FetchContent)
    FetchContent_Declare(metal_test_glm
        URL https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1
        URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)
    FetchContent_GetProperties(metal_test_glm)
    if(NOT metal_test_glm_POPULATED)
        FetchContent_Populate(metal_test_glm)
    endif()
    set(METAL_TEST_GLM_INCLUDE_DIR "${metal_test_glm_SOURCE_DIR}")
endif()
target_include_directories(metal-render-tests PRIVATE "${CORE}/core/render" "${CORE}/environ"
    "${CORE}/plugins/emoteplayer" "${METAL_TEST_GLM_INCLUDE_DIR}")
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
set(TRANSITION_CACHE "${CMAKE_CURRENT_BINARY_DIR}/ProductionTransitionCache.inc")
add_custom_command(OUTPUT "${TRANSITION_CACHE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-transition-cache.py"
        --source "${CORE}/core/script/tjsNativeLayer.cpp" --output "${TRANSITION_CACHE}"
    DEPENDS "${CORE}/core/script/tjsNativeLayer.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-transition-cache.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${TRANSITION_CACHE}")
set(BINDER_DRAW "${CMAKE_CURRENT_BINARY_DIR}/ProductionBinderDraw.inc")
add_custom_command(OUTPUT "${BINDER_DRAW}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-binder-draw.py"
        --source "${CORE}/core/script/tjsNativeLayer.cpp" --output "${BINDER_DRAW}"
    DEPENDS "${CORE}/core/script/tjsNativeLayer.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-binder-draw.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${BINDER_DRAW}")
set(BOUNDED_STACK "${CMAKE_CURRENT_BINARY_DIR}/ProductionBoundedStack.inc")
add_custom_command(OUTPUT "${BOUNDED_STACK}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-cpu-stack.py"
        --source "${CORE}/tjs2/tjsDebug.cpp" --output "${BOUNDED_STACK}"
    DEPENDS "${CORE}/tjs2/tjsDebug.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-cpu-stack.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${BOUNDED_STACK}")
set(IMAGE_ASYNC "${CMAKE_CURRENT_BINARY_DIR}/ProductionImageAsync.inc")
add_custom_command(OUTPUT "${IMAGE_ASYNC}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-image-async.py"
        --source "${CORE}/core/media/image/GraphicsLoadThread.cpp" --output "${IMAGE_ASYNC}"
    DEPENDS "${CORE}/core/media/image/GraphicsLoadThread.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-image-async.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${IMAGE_ASYNC}")
set(IMAGE_HOLDER "${CMAKE_CURRENT_BINARY_DIR}/ProductionImageHolder.inc")
add_custom_command(OUTPUT "${IMAGE_HOLDER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-image-holder.py"
        --source "${CORE}/core/media/image/TVPGraphicsLoader.cpp" --output "${IMAGE_HOLDER}"
    DEPENDS "${CORE}/core/media/image/TVPGraphicsLoader.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-image-holder.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${IMAGE_HOLDER}")
set(STATIC_PRESENTATION_TEST "${CMAKE_CURRENT_BINARY_DIR}/ProductionStaticPresentationTest.inc")
add_custom_command(OUTPUT "${STATIC_PRESENTATION_TEST}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-static-presentation-test.py"
        --source "${CMAKE_CURRENT_LIST_DIR}/NativeStaticPresentationTests.inc" --output "${STATIC_PRESENTATION_TEST}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/NativeStaticPresentationTests.inc" "${CMAKE_CURRENT_LIST_DIR}/extract-static-presentation-test.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${STATIC_PRESENTATION_TEST}")
target_include_directories(metal-render-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CORE}/core/media/image")
add_test(NAME metal-render-tests COMMAND metal-render-tests)
set_tests_properties(metal-render-tests PROPERTIES SKIP_RETURN_CODE 77)

if(APPLE)
    set_tests_properties(metal-render-tests PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME metal-render-compute-tests COMMAND metal-render-tests)
    set_tests_properties(metal-render-compute-tests PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MIKAGE_METAL_LAYER_TILE_RENDERER=0")
    add_test(NAME metal-static-presentation-tests COMMAND metal-render-tests --static-presentation)
    set_tests_properties(metal-static-presentation-tests PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MTL_DEBUG_LAYER=1" TIMEOUT 60)
    add_test(NAME metal-static-presentation-compute-tests COMMAND metal-render-tests --static-presentation)
    set_tests_properties(metal-static-presentation-compute-tests PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MIKAGE_METAL_LAYER_TILE_RENDERER=0" TIMEOUT 60)
endif()
