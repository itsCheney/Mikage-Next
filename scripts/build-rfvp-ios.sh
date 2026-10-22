#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned rfvp fork (FVP, Rust) as RFVPRuntime.xcframework: one
# dynamic framework per SDK holding the rfvp cdylib. It needs a Rust
# toolchain with the iOS targets (rustup target add aarch64-apple-ios
# aarch64-apple-ios-sim). It does not use SDL or FFmpeg: rfvp renders with
# wgpu, plays audio through kira, decodes MP4 with VideoToolbox and WMV and
# MPEG-2 in Rust. It is a framework of its own, not linked with siglus_rs,
# because the two pin different wgpu and kira versions.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/RFVPRuntime/Source"
HEADER="${SOURCE_DIR}/platform/ios/RFVPLauncher/Headers/rfvp.h"
BUILD_ROOT="${PROJECT_DIR}/build/rfvp"
OUTPUT="${PROJECT_DIR}/build/RFVPRuntime.xcframework"
# Matches the other engine runtimes; the C code mimalloc builds needs 13.0.
DEPLOYMENT_TARGET="13.0"
CRATE="rfvp"
FRAMEWORK_NAME="RFVPRuntime"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${SOURCE_DIR}/Cargo.toml" || ! -f "${HEADER}" ]]; then
    echo "rfvp source submodule is missing. Run: git submodule update --init Engine/RFVPRuntime/Source" >&2
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

    # Only the dynamic library: its exports are the C API, and the rlib and
    # the engine's desktop binaries are not needed here.
    SDKROOT="${sdk_path}" \
        cargo rustc --manifest-path "${SOURCE_DIR}/Cargo.toml" \
            -p "${CRATE}" --lib --release --locked \
            --target "${target}" --crate-type cdylib

    local dylib="${CARGO_TARGET_DIR}/${target}/release/lib${CRATE}.dylib"
    if [[ ! -f "${dylib}" ]]; then
        echo "cargo did not produce ${dylib}." >&2
        exit 1
    fi

    local framework="${BUILD_ROOT}/${slice}/${FRAMEWORK_NAME}.framework"
    rm -rf "${framework}"
    mkdir -p "${framework}/Headers" "${framework}/Modules"
    cp "${dylib}" "${framework}/${FRAMEWORK_NAME}"
    install_name_tool -id "@rpath/${FRAMEWORK_NAME}.framework/${FRAMEWORK_NAME}" "${framework}/${FRAMEWORK_NAME}"
    cp "${HEADER}" "${framework}/Headers/rfvp.h"
    cat > "${framework}/Headers/${FRAMEWORK_NAME}.h" <<'EOF'
#include "rfvp.h"
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
    <key>CFBundleIdentifier</key><string>moe.cheney233.mikage.RFVPRuntime</string>
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
    for symbol in rfvp_ios_create rfvp_ios_touch rfvp_ios_destroy rfvp_mikage_step rfvp_mikage_last_error \
        rfvp_mikage_register_font_face rfvp_mikage_register_fallback_font rfvp_mikage_set_script \
        rfvp_mikage_set_save_dir rfvp_mikage_set_log_callback rfvp_mikage_key rfvp_mikage_set_foreground; do
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
