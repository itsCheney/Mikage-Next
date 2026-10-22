#!/usr/bin/env bash
set -euo pipefail

# Fetches MetalANGLE (OpenGL ES on Metal) as build/MetalANGLE.xcframework.
# The shared SDL3 links it for every OpenGL ES window, so Ren'Py's GL
# renderer runs on Metal instead of Apple's deprecated OpenGLES framework.
#
# The binary is the one Ren'Py ships for iOS, from renpy-build's source/
# directory at a pinned commit (MetalANGLE is no longer published upstream).
# It is thinned to arm64 (device) and arm64 + x86_64 (simulator), and its
# minimum OS is raised to SDL's, so Xcode embeds it without warnings.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT="${PROJECT_DIR}/build/MetalANGLE.xcframework"
WORK="${PROJECT_DIR}/build/metalangle"

RENPY_BUILD_COMMIT="fec7881d2e9f68abf868920413d1a4911187ce91"
ARCHIVE_URL="https://raw.githubusercontent.com/renpy/renpy-build/${RENPY_BUILD_COMMIT}/source/MetalANGLE.xcframework.tar.gz"
ARCHIVE_SHA256="c449d5e15746dd146336b19031f6ca51d608548cba0e680f61cee9f4d37d4d26"
# Matches build-sdl3-ios.sh.
DEPLOYMENT_TARGET="13.0"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove it before fetching again." >&2
    exit 1
fi

rm -rf "${WORK}"
mkdir -p "${WORK}/archive" "${WORK}/thin"
curl --fail --location --retry 4 --silent --show-error \
    --output "${WORK}/MetalANGLE.xcframework.tar.gz" "${ARCHIVE_URL}"
actual="$(shasum -a 256 "${WORK}/MetalANGLE.xcframework.tar.gz" | awk '{print $1}')"
if [[ "${actual}" != "${ARCHIVE_SHA256}" ]]; then
    echo "MetalANGLE archive checksum mismatch: ${actual}" >&2
    exit 1
fi
tar -xzf "${WORK}/MetalANGLE.xcframework.tar.gz" -C "${WORK}/archive"

thin_slice() {
    local slice="$1"
    shift
    local source="${WORK}/archive/MetalANGLE.xcframework/${slice}/MetalANGLE.framework"
    local target="${WORK}/thin/${slice}/MetalANGLE.framework"
    if [[ ! -f "${source}/MetalANGLE" ]]; then
        echo "MetalANGLE slice ${slice} is missing from the archive." >&2
        exit 1
    fi
    mkdir -p "$(dirname "${target}")"
    ditto "${source}" "${target}"
    local extract=()
    for arch in "$@"; do
        extract+=(-extract "${arch}")
    done
    lipo "${source}/MetalANGLE" "${extract[@]}" -output "${target}/MetalANGLE"
    plutil -replace MinimumOSVersion -string "${DEPLOYMENT_TARGET}" "${target}/Info.plist"
    install_name="$(otool -D "${target}/MetalANGLE" | tail -n 1)"
    if [[ "${install_name}" != "@rpath/MetalANGLE.framework/MetalANGLE" ]]; then
        echo "Unexpected MetalANGLE install name: ${install_name}" >&2
        exit 1
    fi
}

thin_slice ios-arm64_armv7 arm64
thin_slice ios-arm64_i386_x86_64-simulator arm64 x86_64

xcodebuild -create-xcframework \
    -framework "${WORK}/thin/ios-arm64_armv7/MetalANGLE.framework" \
    -framework "${WORK}/thin/ios-arm64_i386_x86_64-simulator/MetalANGLE.framework" \
    -output "${OUTPUT}"
rm -rf "${WORK}"

test -f "${OUTPUT}/ios-arm64/MetalANGLE.framework/MetalANGLE"
echo "Created ${OUTPUT}"
