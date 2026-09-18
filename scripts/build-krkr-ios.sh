#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VCPKG_BASELINE="8e8dfb4ba483886936ded5ca201b500b8d8b0096"
SOURCE_DIR="${PROJECT_DIR}/Engine/KRKRRuntime/Source"
OUTPUT="${PROJECT_DIR}/build/KRKRRuntime.xcframework"
VCPKG_INSTALLED_DIR="${PROJECT_DIR}/build/krkr-vcpkg-installed"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/CMakeLists.txt" ]]; then
    echo "KRKR source submodule is missing. Run: git submodule update --init --recursive" >&2
    exit 1
fi

# `cpp` is a nested submodule pinned by the build fork. This is a no-op for
# normal developer and CI checkouts, but makes a fresh clone self-contained.
git -C "${SOURCE_DIR}" submodule update --init --recursive
# Verify cache isolation before spending time on the device/simulator builds.
python3 "${PROJECT_DIR}/scripts/check-krkr-scenario-cache.py" --source "${SOURCE_DIR}/cpp"


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
    -DUSE_RENDER_METAL=ON \
    -DVCPKG_INSTALLED_DIR="${VCPKG_INSTALLED_DIR}"
cmake --build "${SOURCE_DIR}/out/ios-device" --config Release --parallel

cmake --preset "iOS Simulator Config" \
    -S "${SOURCE_DIR}" \
    -DKRKR_HOST_LIBRARY=ON \
    -DUSE_RENDER_METAL=ON \
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
