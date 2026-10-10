cmake_minimum_required(VERSION 3.20)
project(MetalRenderBackendTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
add_executable(metal-render-tests
    LayerUploadTests.cpp
    LayerInitializationTests.cpp
    PendingFillTests.cpp
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
set(TRANSITION_CACHE "${CMAKE_CURRENT_BINARY_DIR}/ProductionTransitionCache.inc")
add_custom_command(OUTPUT "${TRANSITION_CACHE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-transition-cache.py"
        --source "${CORE}/core/script/tjsNativeLayer.cpp" --output "${TRANSITION_CACHE}"
    DEPENDS "${CORE}/core/script/tjsNativeLayer.cpp" "${CMAKE_CURRENT_LIST_DIR}/extract-transition-cache.py"
    VERBATIM)
target_sources(metal-render-tests PRIVATE "${TRANSITION_CACHE}")
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
add_executable(c3-upload-emitter LayerUploadEmitter.cpp "${UPLOAD_REPORT}")
target_include_directories(c3-upload-emitter PRIVATE "${CORE}/core/render" "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME c3-upload-parser COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-c3-uploads.py")
set_tests_properties(c3-upload-parser PROPERTIES ENVIRONMENT "C3_UPLOAD_EMITTER=$<TARGET_FILE:c3-upload-emitter>")
set(HOTSPOT_TRACE "${CMAKE_CURRENT_BINARY_DIR}/ProductionHotspotTrace.inc")
add_custom_command(OUTPUT "${HOTSPOT_TRACE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-hotspot-trace.py"
        --source "${CORE}/core/render/backend/MetalRenderBackend.mm" --output "${HOTSPOT_TRACE}"
    DEPENDS "${CORE}/core/render/backend/MetalRenderBackend.mm" "${CMAKE_CURRENT_LIST_DIR}/extract-hotspot-trace.py"
    VERBATIM)
add_executable(p2d-hotspot-emitter LayerHotspotEmitter.cpp "${HOTSPOT_TRACE}")
target_include_directories(p2d-hotspot-emitter PRIVATE "${CORE}/core/render" "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME p2d-hotspot-parser COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-layer-hotspots.py")
set_tests_properties(p2d-hotspot-parser PROPERTIES ENVIRONMENT "P2D_HOTSPOT_EMITTER=$<TARGET_FILE:p2d-hotspot-emitter>")
add_executable(p2d-cpu-emitter CPUFrameEmitter.cpp)
target_include_directories(p2d-cpu-emitter PRIVATE "${CORE}/core/render")
add_test(NAME p2d-cpu-parser COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-cpu-calls.py")
set_tests_properties(p2d-cpu-parser PROPERTIES ENVIRONMENT "P2D_CPU_EMITTER=$<TARGET_FILE:p2d-cpu-emitter>")
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
