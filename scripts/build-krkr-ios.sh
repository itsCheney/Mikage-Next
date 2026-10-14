#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VCPKG_BASELINE="8e8dfb4ba483886936ded5ca201b500b8d8b0096"
SOURCE_DIR="${PROJECT_DIR}/Engine/KRKRRuntime/Source"
OUTPUT="${PROJECT_DIR}/build/KRKRRuntime.xcframework"
# Shared SDL3 built by scripts/build-sdl3-ios.sh. KRKRRuntime links it instead
# of absorbing its own static SDL3 copy; the app embeds it once for all engines.
SDL3_XCFRAMEWORK="${PROJECT_DIR}/build/SDL3.xcframework"
SDL3_DEVICE_FRAMEWORK="${SDL3_XCFRAMEWORK}/ios-arm64/SDL3.framework"
SDL3_SIMULATOR_FRAMEWORK="${SDL3_XCFRAMEWORK}/ios-arm64-simulator/SDL3.framework"
# Engine-neutral SDL lifecycle (main-callback stepping, audio session, reset
# between games) that every SDL engine runtime compiles in.
SDL_HOST_DIR="${PROJECT_DIR}/Engine/MikageSDLHost"
VCPKG_INSTALLED_ROOT="${PROJECT_DIR}/build/krkr-vcpkg-installed"
VCPKG_DEVICE_INSTALLED_DIR="${VCPKG_INSTALLED_ROOT}/device"
VCPKG_SIMULATOR_INSTALLED_DIR="${VCPKG_INSTALLED_ROOT}/simulator"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/CMakeLists.txt" ]]; then
    echo "KRKR source submodule is missing. Run: git submodule update --init --recursive" >&2
    exit 1
fi

if [[ ! -f "${SDL3_DEVICE_FRAMEWORK}/SDL3" || ! -f "${SDL3_SIMULATOR_FRAMEWORK}/SDL3" ]]; then
    echo "Shared SDL3 is missing. Run: bash scripts/build-sdl3-ios.sh" >&2
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
# Keep manifest installs disjoint. Sharing one installed tree makes vcpkg
# reconcile the device and simulator triplets during alternating CMake
# configures, which can discard the work it just restored for the other SDK.
mkdir -p "${VCPKG_DEVICE_INSTALLED_DIR}" "${VCPKG_SIMULATOR_INSTALLED_DIR}"

cmake_launcher_args=()
if command -v ccache >/dev/null 2>&1; then
    # Keep cache paths stable across GitHub-hosted runner workspaces while
    # retaining the compiler and SDK in ccache's normal cache key.
    export CCACHE_DIR="${CCACHE_DIR:-${PROJECT_DIR}/build/krkr-ccache}"
    export CCACHE_BASEDIR="${PROJECT_DIR}"
    # Xcode's per-target header maps arrive as -ivfsoverlay files and its
    # module flags as -fmodules; both are safe to hash by their inputs.
    # ccache only accepts -fmodules in depend mode, which reads the -MMD
    # dependency file Xcode already asks clang for.
    export CCACHE_SLOPPINESS="clang_index_store,ivfsoverlay,modules,include_file_ctime,include_file_mtime,pch_defines,time_macros,system_headers"
    export CCACHE_DEPEND=true
    export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-2G}"
    # The iOS presets use the Xcode generator, which ignores
    # CMAKE_<LANG>_COMPILER_LAUNCHER. Xcode instead runs the CC build
    # setting for every C, C++ and Objective-C file, so point it at
    # wrappers that run the toolchain's clang through ccache. Xcode's build
    # service does not pass this shell's environment to the compiler, so
    # the wrappers carry the ccache settings themselves.
    ccache_bin="${PROJECT_DIR}/build/krkr-ccache-bin"
    mkdir -p "${ccache_bin}"
    for tool in clang clang++; do
        cat > "${ccache_bin}/${tool}" <<EOF
#!/bin/sh
export CCACHE_DIR='${CCACHE_DIR}' CCACHE_BASEDIR='${CCACHE_BASEDIR}'
export CCACHE_SLOPPINESS='${CCACHE_SLOPPINESS}' CCACHE_MAXSIZE='${CCACHE_MAXSIZE}' CCACHE_DEPEND=true
export CCACHE_LOGFILE='${PROJECT_DIR}/build/krkr-ccache.log'
exec '$(command -v ccache)' '$(xcrun -f "${tool}")' "\$@"
EOF
        chmod +x "${ccache_bin}/${tool}"
    done
    cmake_launcher_args=(
        -DCMAKE_XCODE_ATTRIBUTE_CC="${ccache_bin}/clang"
        -DCMAKE_XCODE_ATTRIBUTE_CXX="${ccache_bin}/clang++"
        -DCMAKE_XCODE_ATTRIBUTE_LD="$(xcrun -f clang)"
        -DCMAKE_XCODE_ATTRIBUTE_LDPLUSPLUS="$(xcrun -f clang++)"
        # Xcode's explicitly built modules and the index store cannot be
        # cached; implicit modules are covered by the modules sloppiness.
        -DCMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_EXPLICIT_MODULES=NO
        -DCMAKE_XCODE_ATTRIBUTE_COMPILER_INDEX_STORE_ENABLE=NO
    )
    ccache --zero-stats
fi

cmake --preset "iOS Device Config" \
    -S "${SOURCE_DIR}" \
    -DKRKR_HOST_LIBRARY=ON \
    -DUSE_RENDER_METAL=ON \
    -DKRKR_SDL3_FRAMEWORK="${SDL3_DEVICE_FRAMEWORK}" \
    -DMIKAGE_SDL_HOST_DIR="${SDL_HOST_DIR}" \
    -DVCPKG_INSTALLED_DIR="${VCPKG_DEVICE_INSTALLED_DIR}" \
    "${cmake_launcher_args[@]}"
cmake --build "${SOURCE_DIR}/out/ios-device" --config Release --parallel

cmake --preset "iOS Simulator Config" \
    -S "${SOURCE_DIR}" \
    -DKRKR_HOST_LIBRARY=ON \
    -DUSE_RENDER_METAL=ON \
    -DKRKR_SDL3_FRAMEWORK="${SDL3_SIMULATOR_FRAMEWORK}" \
    -DMIKAGE_SDL_HOST_DIR="${SDL_HOST_DIR}" \
    -DVCPKG_INSTALLED_DIR="${VCPKG_SIMULATOR_INSTALLED_DIR}" \
    "${cmake_launcher_args[@]}"
cmake --build "${SOURCE_DIR}/out/ios-simulator" --config Release --parallel

DEVICE_FRAMEWORK="${SOURCE_DIR}/out/ios-device/Release-iphoneos/KRKRRuntime.framework"
SIMULATOR_FRAMEWORK="${SOURCE_DIR}/out/ios-simulator/Release-iphonesimulator/KRKRRuntime.framework"
if [[ ! -d "${DEVICE_FRAMEWORK}" || ! -d "${SIMULATOR_FRAMEWORK}" ]]; then
    echo "KRKRRuntime.framework was not produced for both device and simulator." >&2
    exit 1
fi

# The runtime must load SDL from the shared framework and carry no SDL code of
# its own: a second copy would register duplicate SDL UIKit classes.
for framework in "${DEVICE_FRAMEWORK}" "${SIMULATOR_FRAMEWORK}"; do
    binary="${framework}/KRKRRuntime"
    if ! otool -L "${binary}" | grep "@rpath/SDL3.framework/SDL3" >/dev/null; then
        echo "${binary} does not link the shared SDL3.framework." >&2
        exit 1
    fi
    if nm -gU "${binary}" | grep -E " _SDL_(Init|CreateWindow|PollEvent)$" >/dev/null; then
        echo "${binary} still contains its own SDL3 implementation." >&2
        exit 1
    fi
    if nm -U "${binary}" | grep "_OBJC_CLASS_\$_SDL_uikit" >/dev/null; then
        echo "${binary} still defines SDL UIKit classes." >&2
        exit 1
    fi
    # Each SDL engine runtime carries its own copy of the shared host, so the
    # copies must stay private to their framework.
    if nm -gU "${binary}" | grep "_MikageSDLHost" >/dev/null; then
        echo "${binary} exports MikageSDLHost symbols." >&2
        exit 1
    fi
done

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
if command -v ccache >/dev/null 2>&1; then
    ccache --show-stats --verbose
    # Why any compile was not cached, most frequent first.
    if [[ -f "${PROJECT_DIR}/build/krkr-ccache.log" ]]; then
        grep -E "Result: |unsupported|Unsupported|Failed|Disabling" "${PROJECT_DIR}/build/krkr-ccache.log" \
            | sed -E 's/^\[[^]]*\] //' | sort | uniq -c | sort -rn | head -n 20 || true
    fi
fi
echo "Created ${OUTPUT}"
