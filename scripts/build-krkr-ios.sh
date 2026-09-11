#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
KRKR_BUILD_COMMIT="66fb7d9533478d33317208cd8ec8696ab9340d6f"
KRKR_CORE_COMMIT="5a8bd422f82d3758045f403520a64b772a59f40c"
VCPKG_BASELINE="8e8dfb4ba483886936ded5ca201b500b8d8b0096"
SOURCE_DIR="${PROJECT_DIR}/build/krkr-source"
OUTPUT="${PROJECT_DIR}/build/KRKRRuntime.xcframework"
VCPKG_INSTALLED_DIR="${PROJECT_DIR}/build/krkr-vcpkg-installed"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/.mikage-host-prepared" ]]; then
    if [[ -e "${SOURCE_DIR}" ]]; then
        echo "${SOURCE_DIR} exists but is not a prepared Mikage source tree." >&2
        exit 1
    fi
    mkdir -p "$(dirname "${SOURCE_DIR}")"
    git init "${SOURCE_DIR}"
    git -C "${SOURCE_DIR}" remote add origin https://github.com/krkrsdl3/krkrsdl3_build.git
    git -C "${SOURCE_DIR}" fetch --depth 1 origin "${KRKR_BUILD_COMMIT}"
    git -C "${SOURCE_DIR}" checkout --detach FETCH_HEAD
    git -C "${SOURCE_DIR}" submodule update --init --depth 1 cpp
    test "$(git -C "${SOURCE_DIR}/cpp" rev-parse HEAD)" = "${KRKR_CORE_COMMIT}"

    git -C "${SOURCE_DIR}" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-build-host.patch"
    python3 -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]); p.write_bytes(p.read_bytes().replace(b"\r\n", b"\n"))' \
        "${SOURCE_DIR}/cpp/environ/sdl3/sdl3_app.cpp"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-core-host.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-lifecycle-host.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-touch-coordinate.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-graphics-cache-lock.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-session-event-reset.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-media-session-reset.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-graphics-session-reset.patch"
    git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-video-overlay-session.patch"
    touch "${SOURCE_DIR}/.mikage-host-prepared"
else
    # Upgrade legacy prepared trees without reverse-checking an earlier patch
    # after a later patch has intentionally changed the same hunk.
    if ! grep -q 'SDL_WINDOW_HIGH_PIXEL_DENSITY' "${SOURCE_DIR}/cpp/environ/sdl3/sdl3_app.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-lifecycle-host.patch"
    fi
    if ! grep -q 'normalizedTouchToDrawable' "${SOURCE_DIR}/cpp/environ/sdl3/sdl3_app.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-touch-coordinate.patch"
    fi
    if ! grep -q 'TVPGraphicCacheMutex' "${SOURCE_DIR}/cpp/core/media/image/TVPGraphicsLoader.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-graphics-cache-lock.patch"
    fi
    if ! grep -q 'TVPResetEventState' "${SOURCE_DIR}/cpp/core/main/TVPEvent.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-session-event-reset.patch"
    fi
    if ! grep -q 'FinalizeSession' "${SOURCE_DIR}/cpp/core/script/tjsNativeVideoOverlay.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-media-session-reset.patch"
    fi
    if ! grep -q 'TVPResetGraphicSessionState' "${SOURCE_DIR}/cpp/core/media/image/TVPGraphicsLoader.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-graphics-session-reset.patch"
    fi
    if ! grep -q 'TVPMoviePlayer::SetVisible(b)' "${SOURCE_DIR}/cpp/core/media/movie/KRMovieOverlay.cpp"; then
        git -C "${SOURCE_DIR}/cpp" apply "${PROJECT_DIR}/Engine/KRKRRuntime/Patches/krkrsdl3-video-overlay-session.patch"
    fi
fi

# Host sources belong to this repository and may change independently of the pinned upstream tree.
mkdir -p "${SOURCE_DIR}/host"
cp "${PROJECT_DIR}/Engine/KRKRRuntime/Host/"* "${SOURCE_DIR}/host/"

if [[ -n "${VCPKG_ROOT:-}" && -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
    export VCPKG_ROOT
elif [[ -n "${VCPKG_INSTALLATION_ROOT:-}" && -f "${VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
    export VCPKG_ROOT="${VCPKG_INSTALLATION_ROOT}"
else
    VCPKG_ROOT="${PROJECT_DIR}/build/vcpkg-${VCPKG_BASELINE}"
    if [[ ! -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]]; then
        git init "${VCPKG_ROOT}"
        git -C "${VCPKG_ROOT}" remote add origin https://github.com/microsoft/vcpkg.git
        git -C "${VCPKG_ROOT}" fetch --depth 1 origin "${VCPKG_BASELINE}"
        git -C "${VCPKG_ROOT}" checkout --detach FETCH_HEAD
        "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
    fi
    export VCPKG_ROOT
fi

export VCPKG_DISABLE_METRICS=1
mkdir -p "${VCPKG_INSTALLED_DIR}"

cmake --preset "iOS Device Config" \
    -S "${SOURCE_DIR}" \
    -DKRKR_HOST_LIBRARY=ON \
    -DVCPKG_INSTALLED_DIR="${VCPKG_INSTALLED_DIR}"
cmake --build "${SOURCE_DIR}/out/ios-device" --config Release --parallel

cmake --preset "iOS Simulator Config" \
    -S "${SOURCE_DIR}" \
    -DKRKR_HOST_LIBRARY=ON \
    -DVCPKG_INSTALLED_DIR="${VCPKG_INSTALLED_DIR}"
cmake --build "${SOURCE_DIR}/out/ios-simulator" --config Release --parallel

DEVICE_FRAMEWORK="${SOURCE_DIR}/out/ios-device/Release-iphoneos/KRKRRuntime.framework"
SIMULATOR_FRAMEWORK="${SOURCE_DIR}/out/ios-simulator/Release-iphonesimulator/KRKRRuntime.framework"
if [[ ! -d "${DEVICE_FRAMEWORK}" || ! -d "${SIMULATOR_FRAMEWORK}" ]]; then
    echo "KRKRRuntime.framework was not produced for both device and simulator." >&2
    exit 1
fi

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
