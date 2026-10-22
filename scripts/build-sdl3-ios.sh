#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned SDL3 submodule as one dynamic SDL3.xcframework that the app
# embeds once and every engine runtime links.
#
# OpenGL ES windows run on MetalANGLE (GLES on Metal) rather than Apple's
# deprecated OpenGLES framework: the build applies Engine/SDL3/patches to a
# copy of the source (the submodule stays clean) and links
# build/MetalANGLE.xcframework, so run scripts/fetch-metalangle-ios.sh first.
# Engines that render with Metal (KiriKiri, and SDL_Renderer's Metal backend)
# are unaffected; Ren'Py's OpenGL ES renderer is what uses it.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/SDL3/Source"
PATCH_DIR="${PROJECT_DIR}/Engine/SDL3/patches"
BUILD_ROOT="${PROJECT_DIR}/build/sdl3"
PATCHED_SOURCE="${BUILD_ROOT}/source"
OUTPUT="${PROJECT_DIR}/build/SDL3.xcframework"
METALANGLE_XCFRAMEWORK="${PROJECT_DIR}/build/MetalANGLE.xcframework"
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

if [[ ! -f "${METALANGLE_XCFRAMEWORK}/ios-arm64/MetalANGLE.framework/MetalANGLE" ]]; then
    echo "MetalANGLE is missing. Run: bash scripts/fetch-metalangle-ios.sh" >&2
    exit 1
fi

rm -rf "${PATCHED_SOURCE}"
mkdir -p "${PATCHED_SOURCE}"
git -C "${SOURCE_DIR}" archive HEAD | tar -x -C "${PATCHED_SOURCE}"
for patch in "${PATCH_DIR}"/*.diff; do
    echo "Applying ${patch##*/}"
    # Not git apply: build/ is inside this repository, and git apply run there
    # silently skips paths outside the current directory.
    patch -p1 --forward --quiet -d "${PATCHED_SOURCE}" < "${patch}"
done

build_slice() {
    local sdk="$1"
    local metalangle_slice="$2"
    local build_dir="${BUILD_ROOT}/${sdk}"
    cmake -S "${PATCHED_SOURCE}" -B "${build_dir}" -G Xcode \
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
        -DSDL_INSTALL=OFF \
        -DSDL_MIKAGE_METALANGLE_DIR="${METALANGLE_XCFRAMEWORK}/${metalangle_slice}" \
        -DCMAKE_XCODE_ATTRIBUTE_CLANG_MODULES_AUTOLINK=NO
    cmake --build "${build_dir}" --config Release --target SDL3-shared --parallel
}

build_slice iphoneos ios-arm64
build_slice iphonesimulator ios-arm64_x86_64-simulator

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
    # GL ES must come from MetalANGLE, never from Apple's OpenGLES.
    libraries="$(otool -L "${framework}/SDL3")"
    if ! grep -q "@rpath/MetalANGLE.framework/MetalANGLE" <<<"${libraries}"; then
        echo "SDL3 does not link MetalANGLE:" >&2
        echo "${libraries}" >&2
        exit 1
    fi
    if grep -q "OpenGLES.framework" <<<"${libraries}"; then
        echo "SDL3 still links Apple's OpenGLES:" >&2
        echo "${libraries}" >&2
        exit 1
    fi
done

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
