#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned MikageYURIS runtime (YU-RIS, Rust) as
# YurisRuntime.xcframework: one dynamic framework per SDK holding the
# yuris-mobile cdylib. It needs a Rust toolchain with the iOS targets
# (rustup target add aarch64-apple-ios aarch64-apple-ios-sim), nothing else:
# the runtime draws with wgpu on Metal and the app pulls its sound. It is a
# framework of its own, as the other Rust engines pin other wgpu versions.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/YurisRuntime/Source"
HEADER="${SOURCE_DIR}/platform/ios/include/yuris_mobile.h"
BUILD_ROOT="${PROJECT_DIR}/build/yuris"
OUTPUT="${PROJECT_DIR}/build/YurisRuntime.xcframework"
DEPLOYMENT_TARGET="15.0"
CRATE="yuris-mobile"
LIBRARY="yuris_mobile"
FRAMEWORK_NAME="YurisRuntime"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/Cargo.toml" || ! -f "${HEADER}" ]]; then
    echo "MikageYURIS source submodule is missing. Run: git submodule update --init Engine/YurisRuntime/Source" >&2
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
#include "yuris_mobile.h"
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
    <key>CFBundleIdentifier</key><string>moe.cheney233.mikage.YurisRuntime</string>
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
    for symbol in yuris_abi_version yuris_last_error yuris_set_log_callback yuris_register_font yuris_clear_fonts \
        yuris_open yuris_screen_size yuris_set_surface yuris_detach_surface yuris_step yuris_pointer yuris_wheel \
        yuris_key yuris_audio yuris_audio_sample_rate yuris_audio_render yuris_audio_release yuris_close; do
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
