#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned MikageSoftPAL runtime (SoftPAL, Rust) as
# SoftPALRuntime.xcframework: one dynamic framework per SDK holding the
# sena-mobile cdylib. It needs a Rust toolchain with the iOS targets
# (rustup target add aarch64-apple-ios aarch64-apple-ios-sim), nothing else:
# the runtime draws with wgpu on Metal and the app pulls its sound. It is a
# framework of its own, as the other Rust engines pin other wgpu versions.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/SoftPALRuntime/Source"
HEADER="${SOURCE_DIR}/platform/ios/include/sena_mobile.h"
BUILD_ROOT="${PROJECT_DIR}/build/softpal"
OUTPUT="${PROJECT_DIR}/build/SoftPALRuntime.xcframework"
DEPLOYMENT_TARGET="15.0"
CRATE="sena-mobile"
LIBRARY="sena_mobile"
FRAMEWORK_NAME="SoftPALRuntime"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/Cargo.toml" || ! -f "${HEADER}" ]]; then
    echo "MikageSoftPAL source submodule is missing. Run: git submodule update --init Engine/SoftPALRuntime/Source" >&2
    exit 1
fi

command -v cargo >/dev/null 2>&1 || { echo "cargo not found; install Rust with rustup." >&2; exit 1; }

export IPHONEOS_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
export CARGO_TARGET_DIR="${BUILD_ROOT}/target"

build_slice() {
    local target="$1"
    local slice="$2"
    local sdk="$3"
    echo "Building ${CRATE} for ${target}"
    local sdk_path
    sdk_path="$(xcrun --sdk "${sdk}" --show-sdk-path)"

    # Only the dynamic library: its exports are the C API.
    SDKROOT="${sdk_path}" \
        cargo rustc --manifest-path "${SOURCE_DIR}/Cargo.toml" \
            -p "${CRATE}" --lib --release --locked \
            --target "${target}" --crate-type cdylib

    local dylib="${CARGO_TARGET_DIR}/${target}/release/lib${LIBRARY}.dylib"
    if [[ ! -f "${dylib}" ]]; then
        echo "cargo did not produce ${dylib}." >&2
        exit 1
    fi

    local framework="${BUILD_ROOT}/${slice}/${FRAMEWORK_NAME}.framework"
    rm -rf "${framework}"
    mkdir -p "${framework}/Headers" "${framework}/Modules"
    cp "${dylib}" "${framework}/${FRAMEWORK_NAME}"
    install_name_tool -id "@rpath/${FRAMEWORK_NAME}.framework/${FRAMEWORK_NAME}" "${framework}/${FRAMEWORK_NAME}"
    cp "${HEADER}" "${framework}/Headers/"
    cat > "${framework}/Headers/${FRAMEWORK_NAME}.h" <<'EOF'
#include "sena_mobile.h"
EOF
    cat > "${framework}/Modules/module.modulemap" <<EOF
framework module ${FRAMEWORK_NAME} {
    umbrella header "${FRAMEWORK_NAME}.h"
    export *
}
EOF
    local platform="iPhoneOS"
    [[ "${sdk}" == "iphonesimulator" ]] && platform="iPhoneSimulator"
    cat > "${framework}/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>${FRAMEWORK_NAME}</string>
    <key>CFBundleIdentifier</key><string>moe.cheney233.mikage.SoftPALRuntime</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>${FRAMEWORK_NAME}</string>
    <key>CFBundlePackageType</key><string>FMWK</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>CFBundleSupportedPlatforms</key><array><string>${platform}</string></array>
    <key>MinimumOSVersion</key><string>${DEPLOYMENT_TARGET}</string>
</dict>
</plist>
EOF

    local binary="${framework}/${FRAMEWORK_NAME}"
    for symbol in sena_mobile_abi_version sena_mobile_last_error sena_mobile_set_log_callback sena_mobile_register_font \
        sena_mobile_open sena_mobile_screen_size sena_mobile_set_surface sena_mobile_detach_surface sena_mobile_step \
        sena_mobile_pointer sena_mobile_wheel sena_mobile_key sena_mobile_set_focused sena_mobile_snapshot \
        sena_mobile_audio sena_mobile_audio_sample_rate sena_mobile_audio_render sena_mobile_audio_release sena_mobile_close; do
        if ! nm -gU "${binary}" | grep " _${symbol}$" >/dev/null; then
            echo "${binary} does not export ${symbol}." >&2
            exit 1
        fi
    done
}

build_slice aarch64-apple-ios ios-arm64 iphoneos
build_slice aarch64-apple-ios-sim ios-arm64-simulator iphonesimulator

xcodebuild -create-xcframework \
    -framework "${BUILD_ROOT}/ios-arm64/${FRAMEWORK_NAME}.framework" \
    -framework "${BUILD_ROOT}/ios-arm64-simulator/${FRAMEWORK_NAME}.framework" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
echo "Created ${OUTPUT}"
