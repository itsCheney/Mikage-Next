cmake_minimum_required(VERSION 3.20)
project(MetalLayerTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
            "${CORE}/core/script/tjsNativeLayer.cpp" "${CORE}/tjs2/tjsNative.h" "${CMAKE_CURRENT_LIST_DIR}/extract-shrink-copy.py" VERBATIM)
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
add_test(NAME metal-layer-tests COMMAND metal-layer-tests)
add_test(NAME metal-layer-capability-audit COMMAND metal-layer-tests --audit-capabilities)
add_test(NAME layer-diagnostics-parser COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test-layer-diagnostics.py")
add_test(NAME app-build-info-wiring COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/check-app-build-info.py")
set_tests_properties(metal-layer-tests PROPERTIES SKIP_RETURN_CODE 77)
if(APPLE)
    set_tests_properties(metal-layer-tests PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
    add_test(NAME metal-layer-compute-tests COMMAND metal-layer-tests)
    set_tests_properties(metal-layer-compute-tests PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MIKAGE_METAL_LAYER_TILE_RENDERER=0")
endif()
