# Compile the entire production runner, geometry and animation code. Only PSB
# resource construction, SDL host hints and GPU command encoding are fixtures.
get_filename_component(NODE_CORE "${CMAKE_CURRENT_LIST_DIR}/../../Engine/KRKRRuntime/Source/cpp" REALPATH)
find_path(EMOTE_GLM_INCLUDE_DIR glm/glm.hpp)
if(NOT EMOTE_GLM_INCLUDE_DIR)
    include(FetchContent)
    FetchContent_Declare(emote_glm
        URL https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1
        URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c)
    FetchContent_GetProperties(emote_glm)
    if(NOT emote_glm_POPULATED)
        FetchContent_Populate(emote_glm)
    endif()
    set(EMOTE_GLM_INCLUDE_DIR "${emote_glm_SOURCE_DIR}")
endif()
add_executable(emote-node-tests "${CMAKE_CURRENT_LIST_DIR}/NodeTests.cpp"
    "${NODE_CORE}/plugins/emoteplayer/emoterunner.cpp"
    "${NODE_CORE}/plugins/emoteplayer/emoteanimation.cpp")
target_compile_features(emote-node-tests PRIVATE cxx_std_17)
target_include_directories(emote-node-tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/NodeStubs" "${EMOTE_GLM_INCLUDE_DIR}"
    "${NODE_CORE}/plugins/emoteplayer" "${NODE_CORE}/plugins" "${NODE_CORE}/core/render"
    "${NODE_CORE}/core/script" "${NODE_CORE}/core/msg" "${NODE_CORE}/tjs2" "${NODE_CORE}/environ")
target_compile_definitions(emote-node-tests PRIVATE TJS_NO_REGEXP=1 _ONLYCONSOLE=1)
if(WIN32)
    target_compile_definitions(emote-node-tests PRIVATE _KRKRSDL3_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(emote-node-tests PRIVATE _KRKRSDL3_MACOS=1)
else()
    target_compile_definitions(emote-node-tests PRIVATE _KRKRSDL3_LINUX=1)
endif()
add_test(NAME emote-node-tests COMMAND emote-node-tests)

# Compile the full production player/adaptor TU as well. Header-only cache
# tests cannot detect missing renderer types in the actual integration file.
add_library(emote-player-compile OBJECT "${NODE_CORE}/plugins/emoteplayer/emoteplayerclass.cpp")
get_target_property(EMOTE_NODE_INCLUDES emote-node-tests INCLUDE_DIRECTORIES)
get_target_property(EMOTE_NODE_DEFINITIONS emote-node-tests COMPILE_DEFINITIONS)
target_include_directories(emote-player-compile PRIVATE ${EMOTE_NODE_INCLUDES})
foreach(dir archive main media/font media/image media/movie media/sound utils utils/math)
    target_include_directories(emote-player-compile PRIVATE "${NODE_CORE}/core/${dir}")
endforeach()
target_compile_definitions(emote-player-compile PRIVATE ${EMOTE_NODE_DEFINITIONS})
target_compile_features(emote-player-compile PRIVATE cxx_std_17)
