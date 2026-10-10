cmake_minimum_required(VERSION 3.20)
project(MetalLayerTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(p2d-context-tests LayerHotspotContextTests.cpp)
target_include_directories(p2d-context-tests PRIVATE "${CORE}/core/render")
set(BITMAP_SOURCE "${CORE}/core/render/LayerBitmap.cpp")
set(GENERATED_OVERWRITE "${CMAKE_CURRENT_BINARY_DIR}/ProductionBitmapOverwrite.inc")
set(GENERATED_CACHES "${CMAKE_CURRENT_BINARY_DIR}/ProductionBitmapCaches.inc")
add_custom_command(OUTPUT "${GENERATED_OVERWRITE}" "${GENERATED_CACHES}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-bitmap-overwrite.py"
            --source "${BITMAP_SOURCE}" --layer-source "${CORE}/core/script/tjsNativeLayer.cpp"
            --trans-source "${CORE}/core/render/TVPTrans.cpp"
            --character-source "${CORE}/core/media/font/CharacterData.cpp" --cache-output "${GENERATED_CACHES}"
            --output "${GENERATED_OVERWRITE}"
    DEPENDS "${BITMAP_SOURCE}" "${CORE}/core/script/tjsNativeLayer.cpp" "${CORE}/core/render/TVPTrans.cpp" "${CORE}/core/media/font/CharacterData.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/extract-bitmap-overwrite.py"
    VERBATIM)
file(GLOB TJS_SOURCES CONFIGURE_DEPENDS "${CORE}/tjs2/*.cpp")
set(UNIV_SHADER_SOURCE "${CORE}/core/render/backend/MetalLayerShaders.h")
set(GENERATED_UNIV_MATH "${CMAKE_CURRENT_BINARY_DIR}/ProductionUnivTransMath.inc")
set(GENERATED_SHRINK_MATH "${CMAKE_CURRENT_BINARY_DIR}/ProductionShrinkMath.inc")
set(GENERATED_SHRINK_COPY "${CMAKE_CURRENT_BINARY_DIR}/ProductionShrinkCopy.inc")
set(GENERATED_LAYEREX_BOUNDARY "${CMAKE_CURRENT_BINARY_DIR}/ProductionLayerExBoundary.inc")
set(GENERATED_SPAN_BINDING "${CMAKE_CURRENT_BINARY_DIR}/ProductionLayerExSpanBinding.inc")
set(GENERATED_SPAN_MATH "${CMAKE_CURRENT_BINARY_DIR}/ProductionSpanMath.inc")
set(GENERATED_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/ProductionLayerExImage.inc")
add_custom_command(OUTPUT "${GENERATED_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-layerex-image.py"
        --core "${CORE}" --output "${GENERATED_IMAGE}"
    DEPENDS "${CORE}/plugins/LayerExImage.cpp" "${CORE}/plugins/LayerExImage.h"
        "${CORE}/plugins/LayerExBase.hpp" "${CORE}/plugins/ScopedLayerPixels.h"
        "${CMAKE_CURRENT_LIST_DIR}/extract-layerex-image.py" VERBATIM)
add_custom_command(OUTPUT "${GENERATED_SPAN_MATH}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-span-math.py"
            --source "${CORE}/core/render/backend/MetalSpanCompositeShaders.h" --output "${GENERATED_SPAN_MATH}"
    DEPENDS "${CORE}/core/render/backend/MetalSpanCompositeShaders.h"
            "${CORE}/core/render/LayerSpanCompositeDefinitions.def"
            "${CMAKE_CURRENT_LIST_DIR}/extract-span-math.py" VERBATIM)
add_custom_command(OUTPUT "${GENERATED_LAYEREX_BOUNDARY}" "${GENERATED_SPAN_BINDING}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-layerex-boundary.py"
            --base-source "${CORE}/plugins/LayerExBase.hpp"
            --draw-source "${CORE}/plugins/LayerExDraw/LayerExDraw.cpp"
            --header-source "${CORE}/plugins/LayerExDraw/LayerExDraw.hpp"
            --scoped-source "${CORE}/plugins/ScopedLayerPixels.h"
            --output "${GENERATED_LAYEREX_BOUNDARY}" --capture-output "${GENERATED_SPAN_BINDING}"
    DEPENDS "${CORE}/plugins/LayerExBase.hpp" "${CORE}/plugins/LayerExDraw/LayerExDraw.cpp"
            "${CORE}/plugins/LayerExDraw/LayerExDraw.hpp" "${CORE}/plugins/ScopedLayerPixels.h"
            "${CORE}/plugins/ncbind/ncbind.hpp" "${CORE}/plugins/ncbind/ncb_invoke.hpp"
            "${CORE}/core/render/CPUConsumerTrace.h" "${CORE}/core/render/RenderManager.h"
            "${CMAKE_CURRENT_LIST_DIR}/extract-layerex-boundary.py" VERBATIM)
add_custom_command(OUTPUT "${GENERATED_SHRINK_MATH}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-shrink-math.py"
            --source "${CORE}/core/render/backend/MetalShrinkShaders.h" --output "${GENERATED_SHRINK_MATH}"
    DEPENDS "${CORE}/core/render/backend/MetalShrinkShaders.h" "${CORE}/core/render/LayerShrink.h"
            "${CORE}/core/render/LayerShrinkDefinitions.def" "${CMAKE_CURRENT_LIST_DIR}/extract-shrink-math.py" VERBATIM)
add_custom_command(OUTPUT "${GENERATED_SHRINK_COPY}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-shrink-copy.py"
            --source "${CORE}/plugins/shrinkCopy.cpp" --scoped-source "${CORE}/plugins/ScopedLayerPixels.h"
            --layer-source "${CORE}/core/script/tjsNativeLayer.cpp" --output "${GENERATED_SHRINK_COPY}"
    DEPENDS "${CORE}/plugins/shrinkCopy.cpp" "${CORE}/plugins/ScopedLayerPixels.h"
            "${CORE}/core/script/tjsNativeLayer.cpp" "${CORE}/tjs2/tjsNative.h"
            "${CORE}/core/render/CPUConsumerTrace.h" "${CMAKE_CURRENT_LIST_DIR}/extract-shrink-copy.py" VERBATIM)
set(GENERATED_TRANSITION_MATH "${CMAKE_CURRENT_BINARY_DIR}/ProductionTransitionMath.inc")
add_custom_command(OUTPUT "${GENERATED_TRANSITION_MATH}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-transition-math.py"
            --source "${CORE}/core/render/backend/MetalTransitionShaders.h" --output "${GENERATED_TRANSITION_MATH}"
    DEPENDS "${CORE}/core/render/backend/MetalTransitionShaders.h" "${CORE}/core/render/LayerTransition.h"
            "${CORE}/core/render/LayerTransitionDefinitions.def" "${CMAKE_CURRENT_LIST_DIR}/extract-transition-math.py"
    VERBATIM)
add_custom_command(OUTPUT "${GENERATED_UNIV_MATH}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-univ-trans-math.py"
            --source "${UNIV_SHADER_SOURCE}" --output "${GENERATED_UNIV_MATH}"
    DEPENDS "${UNIV_SHADER_SOURCE}" "${CMAKE_CURRENT_LIST_DIR}/extract-univ-trans-math.py"
            "${CORE}/core/render/LayerRenderOperation.h" "${CORE}/core/render/LayerOperationDefinitions.def"
    VERBATIM)
set(TRIANGLE_BRIDGE_SOURCE "${CORE}/../host/MikageKRKRRuntime.mm")
set(GENERATED_TRIANGLE_BRIDGE "${CMAKE_CURRENT_BINARY_DIR}/ProductionTriangleProfileBridge.inc")
add_custom_command(OUTPUT "${GENERATED_TRIANGLE_BRIDGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-triangle-profile.py"
            --source "${TRIANGLE_BRIDGE_SOURCE}" --output "${GENERATED_TRIANGLE_BRIDGE}"
    DEPENDS "${TRIANGLE_BRIDGE_SOURCE}" "${CORE}/../host/MikageKRKRRuntime.h"
            "${CMAKE_CURRENT_LIST_DIR}/extract-triangle-profile.py"
    VERBATIM)
set(GENERATED_AFFINE_MATH "${CMAKE_CURRENT_BINARY_DIR}/ProductionAffineMath.inc")
add_custom_command(OUTPUT "${GENERATED_AFFINE_MATH}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/extract-affine-math.py"
            --source "${UNIV_SHADER_SOURCE}" --output "${GENERATED_AFFINE_MATH}"
    DEPENDS "${UNIV_SHADER_SOURCE}" "${CMAKE_CURRENT_LIST_DIR}/extract-affine-math.py"
    VERBATIM)
add_executable(metal-layer-tests MetalLayerTests.cpp BitmapOverwriteTests.cpp TriangleProfileTests.cpp TestPlatform.cpp
    CapabilityAuditTests.cpp CompilationFailureTests.cpp OperationContractTests.cpp C0ProfileTests.cpp
    P1AShaderTests.cpp
    TransitionHandlerTests.cpp TransitionShaderTests.cpp C4ProfileTests.cpp "${GENERATED_TRANSITION_MATH}"
    ShrinkCopyTests.cpp ShrinkShaderTests.cpp C1ProfileTests.cpp "${GENERATED_SHRINK_MATH}" "${GENERATED_SHRINK_COPY}"
    C2ConsumerTraceTests.cpp C2LayerExBoundaryTests.cpp "${GENERATED_LAYEREX_BOUNDARY}"
    C2ImageBindingTests.cpp "${GENERATED_IMAGE}"
    C2SpanShaderTests.cpp C2SpanBindingTests.cpp "${GENERATED_SPAN_MATH}" "${GENERATED_SPAN_BINDING}"
    "${CORE}/plugins/extrans/mosaic.cpp" "${CORE}/plugins/extrans/wave.cpp"
    "${CORE}/plugins/extrans/ripple.cpp" "${CORE}/plugins/extrans/turn.cpp"
    "${CORE}/plugins/extrans/turntrans_table.cpp" "${CORE}/plugins/extrans/rotatebase.cpp"
    "${CORE}/plugins/extrans/rotatetrans.cpp"
    P1BShaderTests.cpp
    P1AGammaTests.cpp
    "${GENERATED_TRIANGLE_BRIDGE}" "${GENERATED_AFFINE_MATH}"
    UnivTransShaderTests.cpp "${GENERATED_UNIV_MATH}" "${GENERATED_OVERWRITE}" "${GENERATED_CACHES}" ${TJS_SOURCES}
    "${CORE}/core/render/ComplexRect.cpp"
    "${CORE}/core/render/RenderManager.cpp"
    "${CORE}/core/render/MetalLayerRenderManager.cpp"
    "${CORE}/core/render/gl/tvpgl.cpp"
    "${CORE}/core/render/gl/blend_function.cpp"
    "${CORE}/core/media/image/BitmapInfomation.cpp")
foreach(dir archive main media/font media/image media/movie media/sound msg render script utils utils/math)
    target_include_directories(metal-layer-tests PRIVATE "${CORE}/core/${dir}")
endforeach()
target_include_directories(metal-layer-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CORE}/tjs2" "${CORE}/environ" "${CORE}/plugins" "${CORE}/plugins/extrans")
target_compile_definitions(metal-layer-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1)
if(WIN32)
    target_include_directories(metal-layer-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../EmotePerformance/NodeStubs")
    target_compile_definitions(metal-layer-tests PRIVATE _KRKRSDL3_WINDOWS=1)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set_target_properties(metal-layer-tests PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    find_package(SDL3 CONFIG REQUIRED)
    target_sources(metal-layer-tests PRIVATE "${CORE}/core/render/backend/MetalRenderBackend.mm")
    set_source_files_properties("${CORE}/core/render/backend/MetalRenderBackend.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(metal-layer-tests PRIVATE _KRKRSDL3_MACOS=1 TEST_NATIVE_METAL=1)
    target_link_libraries(metal-layer-tests PRIVATE SDL3::SDL3 "-framework Foundation" "-framework Metal" "-framework QuartzCore")
else()
    target_include_directories(metal-layer-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../EmotePerformance/NodeStubs")
    target_compile_definitions(metal-layer-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
enable_testing()
add_test(NAME p2d-context-tests COMMAND p2d-context-tests)
set(PLUTOVG_ORACLE_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/plutovg-upstream" CACHE PATH
    "Official unpatched plutovg 1.3.3 tree; missing trees are downloaded with a pinned archive hash")
set(PLUTOVG_PATCHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/plutovg-patched")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/prepare-plutovg-oracle.py"
    --source "${PLUTOVG_ORACLE_SOURCE_DIR}" --download
    --patch "${CORE}/../vcpkg/ports/plutovg/span-capture.patch" --output "${PLUTOVG_PATCHED_DIR}"
    RESULT_VARIABLE PLUTOVG_PREPARED)
if(NOT PLUTOVG_PREPARED EQUAL 0)
    message(FATAL_ERROR "Pinned plutovg CPU/capture oracle preparation failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CORE}/../vcpkg/ports/plutovg/span-capture.patch"
    "${CMAKE_CURRENT_LIST_DIR}/prepare-plutovg-oracle.py"
    "${CMAKE_CURRENT_LIST_DIR}/plutovg-1.3.3-source-hashes.json")
foreach(variant upstream patched)
    if(variant STREQUAL "upstream")
        set(PLUTOVG_TREE "${PLUTOVG_ORACLE_SOURCE_DIR}")
    else()
        set(PLUTOVG_TREE "${PLUTOVG_PATCHED_DIR}")
    endif()
    file(GLOB PLUTOVG_SOURCES "${PLUTOVG_TREE}/source/*.c")
    add_library(plutovg-oracle-${variant} STATIC ${PLUTOVG_SOURCES})
    target_include_directories(plutovg-oracle-${variant} PUBLIC "${PLUTOVG_TREE}/include"
        PRIVATE "${PLUTOVG_TREE}/source")
    target_compile_definitions(plutovg-oracle-${variant} PUBLIC PLUTOVG_BUILD_STATIC
        PRIVATE PLUTOVG_BUILD PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD=1)
    set_target_properties(plutovg-oracle-${variant} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    if(NOT WIN32)
        target_link_libraries(plutovg-oracle-${variant} PUBLIC m)
    endif()
    add_executable(plutovg-${variant}-pixels C2PlutovgOracle.cpp)
    target_link_libraries(plutovg-${variant}-pixels PRIVATE plutovg-oracle-${variant})
endforeach()
target_link_libraries(metal-layer-tests PRIVATE plutovg-oracle-patched)
include("${CMAKE_CURRENT_LIST_DIR}/../TJSBytecodeHash.cmake")
mikage_add_tjs_bytecode_hash(metal-layer-tests "${CORE}")
target_include_directories(metal-layer-tests PRIVATE "${CORE}/plugins/LayerExDraw")
target_compile_definitions(plutovg-patched-pixels PRIVATE TVP_PLUTOVG_CAPTURE_ORACLE=1)
target_include_directories(plutovg-patched-pixels PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CORE}/plugins/LayerExDraw" "${CORE}/core/render")
target_sources(plutovg-patched-pixels PRIVATE "${GENERATED_SPAN_MATH}")
add_test(NAME plutovg-exact-capture COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/compare-plutovg-oracle.py"
    "$<TARGET_FILE:plutovg-upstream-pixels>" "$<TARGET_FILE:plutovg-patched-pixels>")
add_test(NAME metal-layer-tests COMMAND metal-layer-tests)
add_test(NAME metal-layer-capability-audit COMMAND metal-layer-tests --audit-capabilities)
add_test(NAME layer-diagnostics-parser COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-layer-diagnostics.py")
add_test(NAME c2-window-emitter COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-c2-window-emitter.py")
add_test(NAME image-window-emitter COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-image-window-emitter.py")
set_tests_properties(image-window-emitter PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
set_tests_properties(c2-window-emitter PROPERTIES ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
add_test(NAME app-build-info-wiring COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/check-app-build-info.py")
add_test(NAME c2b-profile-window-wiring COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/check-c2b-profile-window.py")
set_tests_properties(c2b-profile-window-wiring PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")
set_tests_properties(metal-layer-tests PROPERTIES SKIP_RETURN_CODE 77)
if(APPLE)
    set_tests_properties(metal-layer-tests PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME metal-layer-compute-tests COMMAND metal-layer-tests)
    set_tests_properties(metal-layer-compute-tests PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MIKAGE_METAL_LAYER_TILE_RENDERER=0")
endif()
