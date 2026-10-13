#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned ONScripter Yuri fork as ONSRuntime.xcframework. It runs
# on the shared SDL3.xcframework through sdl2-compat, so build SDL3 first.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/ONSRuntime/Source/src/onsyuri_mikage"
BUILD_ROOT="${PROJECT_DIR}/build/ons"
OUTPUT="${PROJECT_DIR}/build/ONSRuntime.xcframework"
SDL3_XCFRAMEWORK="${PROJECT_DIR}/build/SDL3.xcframework"
# Matches build-sdl3-ios.sh.
DEPLOYMENT_TARGET="13.0"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/CMakeLists.txt" ]]; then
    echo "ONScripter source submodule is missing. Run: git submodule update --init Engine/ONSRuntime/Source" >&2
    exit 1
fi

if [[ ! -f "${SDL3_XCFRAMEWORK}/ios-arm64/SDL3.framework/SDL3" ]]; then
    echo "Shared SDL3 is missing. Run: bash scripts/build-sdl3-ios.sh" >&2
    exit 1
fi

build_slice() {
    local sdk="$1"
    local sdl3_slice="$2"
    local build_dir="${BUILD_ROOT}/${sdk}"
    cmake -S "${SOURCE_DIR}" -B "${build_dir}" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sdk}" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DMIKAGE_SDL3_FRAMEWORK="${SDL3_XCFRAMEWORK}/${sdl3_slice}/SDL3.framework"
    cmake --build "${build_dir}" --config Release --target ONSRuntime --parallel
}

build_slice iphoneos ios-arm64
build_slice iphonesimulator ios-arm64-simulator

DEVICE_FRAMEWORK="${BUILD_ROOT}/iphoneos/Release-iphoneos/ONSRuntime.framework"
SIMULATOR_FRAMEWORK="${BUILD_ROOT}/iphonesimulator/Release-iphonesimulator/ONSRuntime.framework"
for framework in "${DEVICE_FRAMEWORK}" "${SIMULATOR_FRAMEWORK}"; do
    binary="${framework}/ONSRuntime"
    if [[ ! -f "${binary}" ]]; then
        echo "ONSRuntime.framework was not produced at ${framework}." >&2
        exit 1
    fi
    if [[ "$(otool -D "${binary}" | tail -n 1)" != "@rpath/ONSRuntime.framework/ONSRuntime" ]]; then
        echo "Unexpected ONSRuntime install name: $(otool -D "${binary}" | tail -n 1)" >&2
        exit 1
    fi
    # sdl2-compat loads the app's SDL3 at runtime; a real SDL compiled in
    # would register a second set of SDL UIKit classes.
    if nm -U "${binary}" | grep "_OBJC_CLASS_\$_SDL_uikit" >/dev/null; then
        echo "${binary} contains its own SDL UIKit code." >&2
        exit 1
    fi
    if ! nm -gU "${binary}" | grep " _MikageONSStart$" >/dev/null; then
        echo "${binary} does not export the Mikage host API." >&2
        exit 1
    fi
    if nm -gU "${binary}" | grep -E " _SDL_(Init|CreateWindow|PollEvent)$" >/dev/null; then
        echo "${binary} exports SDL symbols; only the host API should be visible." >&2
        exit 1
    fi
done

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
