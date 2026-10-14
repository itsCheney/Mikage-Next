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
# MIDI music plays through GeneralUser GS v2.0.3 by S. Christian Collins,
# shipped inside the framework with its license.
SOUNDFONT_COMMIT="684543d5e5efaef08d02be50dcda8d552478fa60"
SOUNDFONT_URL="https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${SOUNDFONT_COMMIT}"
SOUNDFONT_SHA256="9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
SOUNDFONT_LICENSE_SHA256="7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be"

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

fetch() {
    local url="$1"
    local destination="$2"
    local sha256="$3"
    if [[ ! -f "${destination}" ]] || ! echo "${sha256}  ${destination}" | shasum -a 256 -c - >/dev/null 2>&1; then
        mkdir -p "$(dirname "${destination}")"
        curl -fL --retry 3 -o "${destination}.part" "${url}"
        echo "${sha256}  ${destination}.part" | shasum -a 256 -c -
        mv "${destination}.part" "${destination}"
    fi
}

# Movies are decoded with the FFmpeg the KiriKiri runtime installs through
# vcpkg (scripts/build-krkr-ios.sh). Without its installed trees, install the
# same manifest here; vcpkg reuses its binary cache when it has one.
KRKR_SOURCE_DIR="${PROJECT_DIR}/Engine/KRKRRuntime/Source"
VCPKG_INSTALLED_ROOT="${PROJECT_DIR}/build/krkr-vcpkg-installed"
VCPKG_BASELINE="8e8dfb4ba483886936ded5ca201b500b8d8b0096"

ensure_ffmpeg() {
    local triplet="$1"
    local installed_dir="$2"
    if [[ -f "${installed_dir}/${triplet}/lib/libavformat.a" ]]; then
        return
    fi
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

ensure_ffmpeg arm64-ios "${VCPKG_INSTALLED_ROOT}/device"
ensure_ffmpeg arm64-ios-simulator "${VCPKG_INSTALLED_ROOT}/simulator"

SOUNDFONT="${BUILD_ROOT}/downloads/GeneralUser-GS.sf2"
SOUNDFONT_LICENSE="${BUILD_ROOT}/downloads/GeneralUser-GS-LICENSE.txt"
fetch "${SOUNDFONT_URL}/GeneralUser-GS.sf2" "${SOUNDFONT}" "${SOUNDFONT_SHA256}"
fetch "${SOUNDFONT_URL}/documentation/LICENSE.txt" "${SOUNDFONT_LICENSE}" "${SOUNDFONT_LICENSE_SHA256}"

build_slice() {
    local sdk="$1"
    local sdl3_slice="$2"
    local ffmpeg_root="$3"
    local build_dir="${BUILD_ROOT}/${sdk}"
    cmake -S "${SOURCE_DIR}" -B "${build_dir}" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sdk}" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DMIKAGE_SDL3_FRAMEWORK="${SDL3_XCFRAMEWORK}/${sdl3_slice}/SDL3.framework" \
        -DMIKAGE_FFMPEG_ROOT="${ffmpeg_root}"
    cmake --build "${build_dir}" --config Release --target ONSRuntime --parallel
}

build_slice iphoneos ios-arm64 "${VCPKG_INSTALLED_ROOT}/device/arm64-ios"
build_slice iphonesimulator ios-arm64-simulator "${VCPKG_INSTALLED_ROOT}/simulator/arm64-ios-simulator"

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
    cp "${SOUNDFONT}" "${SOUNDFONT_LICENSE}" "${framework}/"
done

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
