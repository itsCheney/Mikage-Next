cmake_minimum_required(VERSION 3.20)
project(MetalRenderBackendTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp")
add_executable(metal-render-tests
    MetalRenderBackendTests.cpp
    "${CORE}/core/render/backend/SWRenderBackend.cpp")
target_include_directories(metal-render-tests PRIVATE "${CORE}/core/render" "${CORE}/environ")
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
add_test(NAME metal-render-tests COMMAND metal-render-tests)
set_tests_properties(metal-render-tests PROPERTIES SKIP_RETURN_CODE 77)

if(APPLE)
    set_tests_properties(metal-render-tests PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()
