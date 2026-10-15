#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned openArtemis fork as ArtemisRuntime.xcframework. It links
# the shared SDL3.xcframework and the FFmpeg the KiriKiri build installs, so
# build SDL3 first.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/ArtemisRuntime/Source"
SDL_HOST_DIR="${PROJECT_DIR}/Engine/MikageSDLHost"
BUILD_ROOT="${PROJECT_DIR}/build/artemis"
OUTPUT="${PROJECT_DIR}/build/ArtemisRuntime.xcframework"
SDL3_XCFRAMEWORK="${PROJECT_DIR}/build/SDL3.xcframework"
# Matches build-sdl3-ios.sh and the iOS vcpkg triplets.
DEPLOYMENT_TARGET="13.0"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/src/mikage/CMakeLists.txt" ]]; then
    echo "openArtemis source submodule is missing. Run: git submodule update --init Engine/ArtemisRuntime/Source" >&2
    exit 1
fi

if [[ ! -f "${SDL3_XCFRAMEWORK}/ios-arm64/SDL3.framework/SDL3" ]]; then
    echo "Shared SDL3 is missing. Run: bash scripts/build-sdl3-ios.sh" >&2
    exit 1
fi

# The same vcpkg the KiriKiri build uses: the runner's, or a checkout at the
# shared baseline.
KRKR_SOURCE_DIR="${PROJECT_DIR}/Engine/KRKRRuntime/Source"
KRKR_INSTALLED_ROOT="${PROJECT_DIR}/build/krkr-vcpkg-installed"
VCPKG_BASELINE="8e8dfb4ba483886936ded5ca201b500b8d8b0096"
if [[ -n "${VCPKG_ROOT:-}" && -x "${VCPKG_ROOT}/vcpkg" ]]; then
    :
elif [[ -n "${VCPKG_INSTALLATION_ROOT:-}" && -x "${VCPKG_INSTALLATION_ROOT}/vcpkg" ]]; then
    VCPKG_ROOT="${VCPKG_INSTALLATION_ROOT}"
else
    VCPKG_ROOT="${PROJECT_DIR}/build/vcpkg-${VCPKG_BASELINE}"
    if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
        git init "${VCPKG_ROOT}"
        git -C "${VCPKG_ROOT}" remote add origin https://github.com/microsoft/vcpkg.git
        git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}"
        git -C "${VCPKG_ROOT}" checkout --detach FETCH_HEAD
        "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
    fi
fi
export VCPKG_ROOT VCPKG_DISABLE_METRICS=1

# Movies are decoded with the FFmpeg the KiriKiri runtime installs through
# vcpkg (scripts/build-krkr-ios.sh). Without its installed trees, install the
# same manifest here; vcpkg reuses its binary cache when it has one.
ensure_ffmpeg() {
    local triplet="$1"
    local installed_dir="$2"
    if [[ -f "${installed_dir}/${triplet}/lib/libavformat.a" ]]; then
        return
    fi
    mkdir -p "${installed_dir}"
    "${VCPKG_ROOT}/vcpkg" install \
        --x-manifest-root="${KRKR_SOURCE_DIR}" \
        --x-install-root="${installed_dir}" \
        --triplet="${triplet}"
    if [[ ! -f "${installed_dir}/${triplet}/lib/libavformat.a" ]]; then
        echo "vcpkg did not install FFmpeg for ${triplet}." >&2
        exit 1
    fi
}

ensure_ffmpeg arm64-ios "${KRKR_INSTALLED_ROOT}/device"
ensure_ffmpeg arm64-ios-simulator "${KRKR_INSTALLED_ROOT}/simulator"

# openArtemis's own dependencies (freetype, libpng, libjpeg-turbo,
# libvorbis, libtheora, physfs) come from its manifest, which leaves SDL3
# and FFmpeg out on iOS. Its iOS triplets match KiriKiri's, so packages both
# use come from the shared binary cache. One installed tree per SDK, as for
# KiriKiri.
ARTEMIS_INSTALLED_ROOT="${PROJECT_DIR}/build/artemis-vcpkg-installed"

build_slice() {
    local sdk="$1"
    local triplet="$2"
    local sdl3_slice="$3"
    local installed_dir="$4"
    local ffmpeg_root="$5"
    local build_dir="${BUILD_ROOT}/${sdk}"
    mkdir -p "${installed_dir}"
    "${VCPKG_ROOT}/vcpkg" install \
        --x-manifest-root="${SOURCE_DIR}" \
        --x-install-root="${installed_dir}" \
        --overlay-triplets="${SOURCE_DIR}/cmake/triplets" \
        --triplet="${triplet}"
    cmake -S "${SOURCE_DIR}" -B "${build_dir}" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sdk}" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET="${triplet}" \
        -DVCPKG_OVERLAY_TRIPLETS="${SOURCE_DIR}/cmake/triplets" \
        -DVCPKG_INSTALLED_DIR="${installed_dir}" \
        -DVCPKG_MANIFEST_INSTALL=OFF \
        -DOA_MIKAGE_RUNTIME=ON \
        -DOA_BUILD_TOOLS=OFF \
        -DOA_ENABLE_TESTS=OFF \
        -DOA_SDL3_FRAMEWORK="${SDL3_XCFRAMEWORK}/${sdl3_slice}/SDL3.framework" \
        -DOA_FFMPEG_ROOT="${ffmpeg_root}" \
        -DMIKAGE_SDL_HOST_DIR="${SDL_HOST_DIR}"
    cmake --build "${build_dir}" --config Release --target ArtemisRuntime --parallel
}

build_slice iphoneos arm64-ios ios-arm64 \
    "${ARTEMIS_INSTALLED_ROOT}/device" "${KRKR_INSTALLED_ROOT}/device/arm64-ios"
build_slice iphonesimulator arm64-ios-simulator ios-arm64-simulator \
    "${ARTEMIS_INSTALLED_ROOT}/simulator" "${KRKR_INSTALLED_ROOT}/simulator/arm64-ios-simulator"

DEVICE_FRAMEWORK="${BUILD_ROOT}/iphoneos/src/mikage/Release-iphoneos/ArtemisRuntime.framework"
SIMULATOR_FRAMEWORK="${BUILD_ROOT}/iphonesimulator/src/mikage/Release-iphonesimulator/ArtemisRuntime.framework"
for framework in "${DEVICE_FRAMEWORK}" "${SIMULATOR_FRAMEWORK}"; do
    binary="${framework}/ArtemisRuntime"
    if [[ ! -f "${binary}" ]]; then
        echo "ArtemisRuntime.framework was not produced at ${framework}." >&2
        find "${BUILD_ROOT}" -name "ArtemisRuntime.framework" -maxdepth 6 >&2 || true
        exit 1
    fi
    if [[ "$(otool -D "${binary}" | tail -n 1)" != "@rpath/ArtemisRuntime.framework/ArtemisRuntime" ]]; then
        echo "Unexpected ArtemisRuntime install name: $(otool -D "${binary}" | tail -n 1)" >&2
        exit 1
    fi
    # SDL comes from the app's shared SDL3.framework; a copy compiled in
    # would register a second set of SDL UIKit classes.
    if nm -U "${binary}" | grep "_OBJC_CLASS_\$_SDL_uikit" >/dev/null; then
        echo "${binary} contains its own SDL UIKit code." >&2
        exit 1
    fi
    if ! otool -L "${binary}" | grep "@rpath/SDL3.framework/SDL3" >/dev/null; then
        echo "${binary} does not link the shared SDL3.framework." >&2
        exit 1
    fi
    if ! nm -gU "${binary}" | grep " _MikageArtemisStart$" >/dev/null; then
        echo "${binary} does not export the Mikage host API." >&2
        exit 1
    fi
    if nm -gU "${binary}" | grep -E " _(SDL_App[A-Za-z]+|MikageSDLHost[A-Za-z]+)$" >/dev/null; then
        echo "${binary} exports engine symbols; only the host API should be visible." >&2
        exit 1
    fi
done

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
