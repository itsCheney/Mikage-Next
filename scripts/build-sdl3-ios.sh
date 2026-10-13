#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned SDL3 submodule as one dynamic SDL3.xcframework that the app
# embeds once and every engine runtime links (KRKRRuntime today).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/SDL3/Source"
BUILD_ROOT="${PROJECT_DIR}/build/sdl3"
OUTPUT="${PROJECT_DIR}/build/SDL3.xcframework"
# Matches the vcpkg iOS triplets, so the shared library supports every OS the
# runtimes built against vcpkg headers do.
DEPLOYMENT_TARGET="13.0"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/CMakeLists.txt" ]]; then
    echo "SDL3 source submodule is missing. Run: git submodule update --init Engine/SDL3/Source" >&2
    exit 1
fi

build_slice() {
    local sdk="$1"
    local build_dir="${BUILD_ROOT}/${sdk}"
    cmake -S "${SOURCE_DIR}" -B "${build_dir}" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sdk}" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DSDL_SHARED=ON \
        -DSDL_STATIC=OFF \
        -DSDL_FRAMEWORK=ON \
        -DSDL_LIBC=ON \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_INSTALL=OFF
    cmake --build "${build_dir}" --config Release --target SDL3-shared --parallel
}

build_slice iphoneos
build_slice iphonesimulator

DEVICE_FRAMEWORK="${BUILD_ROOT}/iphoneos/Release-iphoneos/SDL3.framework"
SIMULATOR_FRAMEWORK="${BUILD_ROOT}/iphonesimulator/Release-iphonesimulator/SDL3.framework"
for framework in "${DEVICE_FRAMEWORK}" "${SIMULATOR_FRAMEWORK}"; do
    if [[ ! -f "${framework}/SDL3" ]]; then
        echo "SDL3.framework was not produced at ${framework}." >&2
        exit 1
    fi
    # Runtimes resolve SDL through the app's Frameworks rpath.
    install_name="$(otool -D "${framework}/SDL3" | tail -n 1)"
    if [[ "${install_name}" != "@rpath/SDL3.framework/SDL3" ]]; then
        echo "Unexpected SDL3 install name: ${install_name}" >&2
        exit 1
    fi
done

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
