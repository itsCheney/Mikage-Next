#!/usr/bin/env bash
set -euo pipefail

# Builds the pinned Ren'Py fork as RenPyRuntime.xcframework: CPython 3.12,
# Ren'Py's Cython modules and pyobjus compiled into one framework, with the
# Python standard library and Ren'Py's Python code in its base/ folder.
#
# It links the shared SDL3.xcframework (whose OpenGL ES runs on MetalANGLE)
# and the FFmpeg the KiriKiri build installs, so run fetch-metalangle-ios.sh
# and build-sdl3-ios.sh first. A host Python 3.12 is needed to cross-build
# CPython and to generate Ren'Py's C from Cython: set RENPY_HOST_PYTHON, or
# have python3.12 on PATH.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOURCE_DIR="${PROJECT_DIR}/Engine/RenPyRuntime/Source"
MIKAGE_DIR="${SOURCE_DIR}/mikage"
BUILD_ROOT="${PROJECT_DIR}/build/renpy"
OUTPUT="${PROJECT_DIR}/build/RenPyRuntime.xcframework"
SDL3_XCFRAMEWORK="${PROJECT_DIR}/build/SDL3.xcframework"
METALANGLE_XCFRAMEWORK="${PROJECT_DIR}/build/MetalANGLE.xcframework"
# Matches build-sdl3-ios.sh and the iOS vcpkg triplets.
DEPLOYMENT_TARGET="13.0"

PYTHON_VERSION="3.12.8"
PYTHON_URL="https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tar.xz"
PYTHON_SHA256="c909157bb25ec114e5869124cc2a9c4a4d4c1e957ca4ff553f1edc692101154e"
CYTHON_VERSION="3.3.0"
# The pyobjus commit renpy-build uses for iOS.
PYOBJUS_COMMIT="b61d65fdc620dbe19892d2419b77340341e76084"

if [[ -e "${OUTPUT}" ]]; then
    echo "${OUTPUT} already exists; remove the generated build directory before rebuilding." >&2
    exit 1
fi

if [[ ! -f "${MIKAGE_DIR}/CMakeLists.txt" ]]; then
    echo "Ren'Py source submodule is missing. Run: git submodule update --init Engine/RenPyRuntime/Source" >&2
    exit 1
fi

if [[ ! -f "${SDL3_XCFRAMEWORK}/ios-arm64/SDL3.framework/SDL3" ]]; then
    echo "Shared SDL3 is missing. Run: bash scripts/build-sdl3-ios.sh" >&2
    exit 1
fi

if [[ ! -f "${METALANGLE_XCFRAMEWORK}/ios-arm64/MetalANGLE.framework/MetalANGLE" ]]; then
    echo "MetalANGLE is missing. Run: bash scripts/fetch-metalangle-ios.sh" >&2
    exit 1
fi

HOST_PYTHON="${RENPY_HOST_PYTHON:-$(command -v python3.12 || true)}"
if [[ -z "${HOST_PYTHON}" ]] || ! "${HOST_PYTHON}" -c 'import sys; sys.exit(sys.version_info[:2] != (3, 12))'; then
    echo "A host Python 3.12 is required (set RENPY_HOST_PYTHON)." >&2
    exit 1
fi

mkdir -p "${BUILD_ROOT}"

# --- Host tools: Cython for Ren'Py and pyobjus -------------------------------

HOST_VENV="${BUILD_ROOT}/host-venv"
if [[ ! -x "${HOST_VENV}/bin/cython" ]]; then
    "${HOST_PYTHON}" -m venv "${HOST_VENV}"
    "${HOST_VENV}/bin/python" -m pip install --quiet "cython==${CYTHON_VERSION}" setuptools
fi
VENV_PYTHON="${HOST_VENV}/bin/python"

# Ren'Py's Cython modules become C under tmp/gen, listed in src/Setup.
(cd "${SOURCE_DIR}" && "${VENV_PYTHON}" setup.py generate)
test -f "${SOURCE_DIR}/src/Setup"

# The Ren'Py version, from the source checkout (renpy/vc_version.py).
(cd "${SOURCE_DIR}" && "${VENV_PYTHON}" -c 'import renpy.versions as v; v.generate_vc_version()')
RENPY_VERSION="$(cd "${SOURCE_DIR}" && "${VENV_PYTHON}" -c \
    'import renpy.vc_version as v; print(".".join(v.version.split(".")[:3]))')"
echo "Ren'Py ${RENPY_VERSION}"

PYOBJUS_SRC="${BUILD_ROOT}/pyobjus-src"
PYOBJUS_GEN="${BUILD_ROOT}/pyobjus"
if [[ ! -f "${PYOBJUS_GEN}/pyobjus.c" ]]; then
    rm -rf "${PYOBJUS_SRC}" "${PYOBJUS_GEN}"
    git init --quiet "${PYOBJUS_SRC}"
    git -C "${PYOBJUS_SRC}" remote add origin https://github.com/kivy/pyobjus.git
    git -C "${PYOBJUS_SRC}" fetch --quiet --depth 1 origin "${PYOBJUS_COMMIT}"
    git -C "${PYOBJUS_SRC}" checkout --quiet --detach FETCH_HEAD
    git -C "${PYOBJUS_SRC}" apply "${MIKAGE_DIR}/pyobjus/pyobjus-ffi-h.diff"
    cat > "${PYOBJUS_SRC}/pyobjus/config.pxi" <<'EOF'
DEF PLATFORM = "ios"
DEF ARCH = "arm64"
DEF PYOBJUS_CYTHON_3 = True
EOF
    (cd "${PYOBJUS_SRC}/pyobjus" && "${HOST_VENV}/bin/cython" pyobjus.pyx)
    mkdir -p "${PYOBJUS_GEN}/python/pyobjus"
    cp "${PYOBJUS_SRC}/pyobjus/pyobjus.c" "${PYOBJUS_SRC}/pyobjus/_runtime.h" "${PYOBJUS_GEN}/"
    for file in __init__.py dylib_manager.py objc_py_types.py protocols.py; do
        cp "${PYOBJUS_SRC}/pyobjus/${file}" "${PYOBJUS_GEN}/python/pyobjus/"
    done
fi

# --- vcpkg: native dependencies ----------------------------------------------

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
# vcpkg (scripts/build-krkr-ios.sh), as openArtemis does.
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
        --overlay-ports="${KRKR_SOURCE_DIR}/vcpkg/ports" \
        --overlay-triplets="${KRKR_SOURCE_DIR}/vcpkg/triplets" \
        --triplet="${triplet}"
    if [[ ! -f "${installed_dir}/${triplet}/lib/libavformat.a" ]]; then
        echo "vcpkg did not install FFmpeg for ${triplet}." >&2
        exit 1
    fi
}

ensure_ffmpeg arm64-ios "${KRKR_INSTALLED_ROOT}/device"
ensure_ffmpeg arm64-ios-simulator "${KRKR_INSTALLED_ROOT}/simulator"

# Ren'Py's own libraries (SDL3_image, FreeType, HarfBuzz, FriBidi, assimp,
# libffi, ...) from mikage/vcpkg.json. Its triplets are KiriKiri's plus a
# cross-build host for libffi.
RENPY_INSTALLED_ROOT="${PROJECT_DIR}/build/renpy-vcpkg-installed"
install_deps() {
    local triplet="$1"
    local installed_dir="$2"
    mkdir -p "${installed_dir}"
    "${VCPKG_ROOT}/vcpkg" install \
        --x-manifest-root="${MIKAGE_DIR}" \
        --x-install-root="${installed_dir}" \
        --overlay-triplets="${MIKAGE_DIR}/triplets" \
        --triplet="${triplet}"
}

install_deps arm64-ios "${RENPY_INSTALLED_ROOT}/device"
install_deps arm64-ios-simulator "${RENPY_INSTALLED_ROOT}/simulator"

# --- CPython 3.12, cross-built for iOS ---------------------------------------

PYTHON_ARCHIVE="${BUILD_ROOT}/Python-${PYTHON_VERSION}.tar.xz"
if [[ ! -f "${PYTHON_ARCHIVE}" ]]; then
    curl --fail --location --retry 4 --silent --show-error --output "${PYTHON_ARCHIVE}.part" "${PYTHON_URL}"
    mv "${PYTHON_ARCHIVE}.part" "${PYTHON_ARCHIVE}"
fi
actual="$(shasum -a 256 "${PYTHON_ARCHIVE}" | awk '{print $1}')"
if [[ "${actual}" != "${PYTHON_SHA256}" ]]; then
    echo "Python archive checksum mismatch: ${actual}" >&2
    exit 1
fi

# Static libpython with every module in mikage/python/Setup.stdlib built in,
# configured the way renpy-build configures it for iOS.
build_python() {
    local sdk="$1"
    local target="$2"
    local deps="$3"
    local prefix="$4"
    if [[ -f "${prefix}/include/python3.12/Python.h" ]]; then
        return
    fi
    local work="${BUILD_ROOT}/python-${sdk}"
    rm -rf "${work}" "${prefix}"
    mkdir -p "${work}"
    tar -xf "${PYTHON_ARCHIVE}" -C "${work}"
    local src="${work}/Python-${PYTHON_VERSION}"
    patch -d "${src}" -p1 < "${MIKAGE_DIR}/python/cross-darwin.diff"
    patch -d "${src}" -p1 < "${MIKAGE_DIR}/python/ios-posixmodule.diff"
    cp "${MIKAGE_DIR}/python/_scproxy.c" "${src}/Modules/_scproxy.c"
    cp "${MIKAGE_DIR}/python/config.site" "${src}/config.site"
    # configure renders Setup.stdlib from this template, so the Makefile it
    # writes already builds exactly these modules in.
    cp "${MIKAGE_DIR}/python/Setup.stdlib" "${src}/Modules/Setup.stdlib.in"

    local sysroot
    sysroot="$(xcrun --sdk "${sdk}" --show-sdk-path)"
    local flags="-target ${target} -isysroot ${sysroot}"
    (
        cd "${src}"
        unset MACOSX_DEPLOYMENT_TARGET
        export IPHONEOS_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
        export CONFIG_SITE="${src}/config.site"
        export CC="$(xcrun --sdk "${sdk}" -f clang) ${flags}"
        export CPP="${CC} -E"
        export AR="$(xcrun --sdk "${sdk}" -f ar)"
        export RANLIB="$(xcrun --sdk "${sdk}" -f ranlib)"
        export CFLAGS="-O2 -I${deps}/include"
        export CPPFLAGS="-I${deps}/include"
        export LDFLAGS="-L${deps}/lib"
        export LIBFFI_CFLAGS="-I${deps}/include"
        export LIBFFI_LIBS="-L${deps}/lib -lffi"
        ./configure \
            --host=arm-apple-darwin \
            --build="$(./config.guess)" \
            --prefix="${prefix}" \
            --disable-toolbox-glue \
            --disable-test-modules \
            --enable-ipv6 \
            --with-build-python="${HOST_PYTHON}" \
            --with-ensurepip=no
        make -j "$(sysctl -n hw.ncpu)"
        make install
    )
    test -f "${prefix}/include/python3.12/Python.h"
}

build_python iphoneos "arm64-apple-ios${DEPLOYMENT_TARGET}" \
    "${RENPY_INSTALLED_ROOT}/device/arm64-ios" "${BUILD_ROOT}/python-install-iphoneos"
build_python iphonesimulator "arm64-apple-ios${DEPLOYMENT_TARGET}-simulator" \
    "${RENPY_INSTALLED_ROOT}/simulator/arm64-ios-simulator" "${BUILD_ROOT}/python-install-iphonesimulator"

# --- base/: the Python the framework runs ------------------------------------

# Everything is precompiled with the host Python 3.12 (same bytecode), since
# the app bundle is read-only. The standard library goes in one zip.
assemble_base() {
    local python_prefix="$1"
    local base="$2"
    rm -rf "${base}"
    mkdir -p "${base}/lib/python3.12"
    "${VENV_PYTHON}" - "${python_prefix}/lib/python3.12" "${base}/lib/python312.zip" <<'EOF'
import compileall, pathlib, py_compile, sys, zipfile

stdlib = pathlib.Path(sys.argv[1])
skip = {"test", "idlelib", "tkinter", "turtledemo", "ensurepip", "venv", "lib2to3",
        "pydoc_data", "site-packages", "lib-dynload", "__pycache__", "unittest/test"}
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(stdlib.rglob("*.py")):
        relative = path.relative_to(stdlib)
        if relative.parts[0] in skip or relative.parts[0].startswith("config-") or "__pycache__" in relative.parts:
            continue
        if "test" in relative.parts or "tests" in relative.parts:
            continue
        compiled = py_compile.compile(str(path), cfile=str(path) + "c", doraise=True,
                                      invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)
        archive.write(compiled, str(relative) + "c")
EOF

    # Ren'Py: renpy.py, the renpy package (Python, common/ and the other
    # files it reads), plus the bridge's session module and pyobjus.
    cp "${SOURCE_DIR}/renpy.py" "${MIKAGE_DIR}/mikage_session.py" "${base}/"
    "${VENV_PYTHON}" -c 'import shutil, sys
shutil.copytree(sys.argv[1], sys.argv[2], ignore=shutil.ignore_patterns(
    "__pycache__", "*.pyc", "*.pyx", "*.pxd", "*.pxi", "*.pyi", "*.c", "*.h", "*.rpyc"))' \
        "${SOURCE_DIR}/renpy" "${base}/renpy"
    cp -R "${PYOBJUS_GEN}/python/pyobjus" "${base}/"
    "${VENV_PYTHON}" -m compileall -q -j 0 --invalidation-mode unchecked-hash \
        "${base}/renpy" "${base}/pyobjus" "${base}/mikage_session.py"
}

# --- RenPyRuntime.framework per SDK ------------------------------------------

# Compile through ccache, set up as in build-artemis-ios.sh.
cmake_launcher_args=()
if command -v ccache >/dev/null 2>&1; then
    export CCACHE_DIR="${CCACHE_DIR:-${PROJECT_DIR}/build/renpy-ccache}"
    export CCACHE_BASEDIR="${PROJECT_DIR}"
    export CCACHE_SLOPPINESS="clang_index_store,ivfsoverlay,modules,include_file_ctime,include_file_mtime,pch_defines,time_macros,system_headers"
    export CCACHE_DEPEND=true
    export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-1G}"
    ccache_bin="${PROJECT_DIR}/build/renpy-ccache-bin"
    mkdir -p "${ccache_bin}"
    for tool in clang clang++; do
        cat > "${ccache_bin}/${tool}" <<EOF
#!/bin/sh
export CCACHE_DIR='${CCACHE_DIR}' CCACHE_BASEDIR='${CCACHE_BASEDIR}'
export CCACHE_SLOPPINESS='${CCACHE_SLOPPINESS}' CCACHE_MAXSIZE='${CCACHE_MAXSIZE}' CCACHE_DEPEND=true
export CCACHE_LOGFILE='${PROJECT_DIR}/build/renpy-ccache.log'
exec '$(command -v ccache)' '$(xcrun -f "${tool}")' "\$@"
EOF
        chmod +x "${ccache_bin}/${tool}"
    done
    cmake_launcher_args=(
        -DCMAKE_XCODE_ATTRIBUTE_CC="${ccache_bin}/clang"
        -DCMAKE_XCODE_ATTRIBUTE_CXX="${ccache_bin}/clang++"
        -DCMAKE_XCODE_ATTRIBUTE_LD="$(xcrun -f clang)"
        -DCMAKE_XCODE_ATTRIBUTE_LDPLUSPLUS="$(xcrun -f clang++)"
        -DCMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_EXPLICIT_MODULES=NO
        -DCMAKE_XCODE_ATTRIBUTE_COMPILER_INDEX_STORE_ENABLE=NO
    )
    ccache --zero-stats
fi

build_slice() {
    local sdk="$1"
    local deps="$2"
    local ffmpeg_root="$3"
    local sdl3_slice="$4"
    local metalangle_slice="$5"
    local build_dir="${BUILD_ROOT}/${sdk}"
    cmake -S "${MIKAGE_DIR}" -B "${build_dir}" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sdk}" \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DRENPY_PYTHON_PREFIX="${BUILD_ROOT}/python-install-${sdk}" \
        -DRENPY_DEPS_PREFIX="${deps}" \
        -DRENPY_FFMPEG_ROOT="${ffmpeg_root}" \
        -DRENPY_SDL3_FRAMEWORK="${SDL3_XCFRAMEWORK}/${sdl3_slice}/SDL3.framework" \
        -DRENPY_METALANGLE_FRAMEWORK="${METALANGLE_XCFRAMEWORK}/${metalangle_slice}/MetalANGLE.framework" \
        -DRENPY_PYOBJUS_DIR="${PYOBJUS_GEN}" \
        -DRENPY_VERSION_STRING="${RENPY_VERSION}" \
        ${cmake_launcher_args[@]+"${cmake_launcher_args[@]}"}
    cmake --build "${build_dir}" --config Release --target RenPyRuntime --parallel
}

build_slice iphoneos "${RENPY_INSTALLED_ROOT}/device/arm64-ios" \
    "${KRKR_INSTALLED_ROOT}/device/arm64-ios" ios-arm64 ios-arm64
build_slice iphonesimulator "${RENPY_INSTALLED_ROOT}/simulator/arm64-ios-simulator" \
    "${KRKR_INSTALLED_ROOT}/simulator/arm64-ios-simulator" ios-arm64-simulator ios-arm64_x86_64-simulator

DEVICE_FRAMEWORK="${BUILD_ROOT}/iphoneos/Release-iphoneos/RenPyRuntime.framework"
SIMULATOR_FRAMEWORK="${BUILD_ROOT}/iphonesimulator/Release-iphonesimulator/RenPyRuntime.framework"
for sdk in iphoneos iphonesimulator; do
    if [[ "${sdk}" == iphoneos ]]; then framework="${DEVICE_FRAMEWORK}"; else framework="${SIMULATOR_FRAMEWORK}"; fi
    binary="${framework}/RenPyRuntime"
    if [[ ! -f "${binary}" ]]; then
        echo "RenPyRuntime.framework was not produced at ${framework}." >&2
        find "${BUILD_ROOT}/${sdk}" -name "RenPyRuntime.framework" -maxdepth 4 >&2 || true
        exit 1
    fi
    if [[ "$(otool -D "${binary}" | tail -n 1)" != "@rpath/RenPyRuntime.framework/RenPyRuntime" ]]; then
        echo "Unexpected RenPyRuntime install name: $(otool -D "${binary}" | tail -n 1)" >&2
        exit 1
    fi
    libraries="$(otool -L "${binary}")"
    # SDL comes from the app's shared SDL3.framework; a copy compiled in
    # would register a second set of SDL UIKit classes.
    if nm -U "${binary}" | grep "_OBJC_CLASS_\$_SDL_uikit" >/dev/null; then
        echo "${binary} contains its own SDL UIKit code." >&2
        exit 1
    fi
    if ! grep -q "@rpath/SDL3.framework/SDL3" <<<"${libraries}"; then
        echo "${binary} does not link the shared SDL3.framework." >&2
        exit 1
    fi
    if ! grep -q "@rpath/MetalANGLE.framework/MetalANGLE" <<<"${libraries}" \
        || grep -q "OpenGLES.framework" <<<"${libraries}"; then
        echo "${binary} must take OpenGL ES from MetalANGLE only:" >&2
        echo "${libraries}" >&2
        exit 1
    fi
    if ! nm -gU "${binary}" | grep " _MikageRenPyStart$" >/dev/null; then
        echo "${binary} does not export the Mikage host API." >&2
        exit 1
    fi
    if nm -gU "${binary}" | grep -E " _(Py[A-Za-z_]+|PyInit_[A-Za-z_]+|SDL_[A-Za-z]+)$" >/dev/null; then
        echo "${binary} exports engine symbols; only the host API should be visible." >&2
        exit 1
    fi
    assemble_base "${BUILD_ROOT}/python-install-${sdk}" "${framework}/base"
    test -f "${framework}/base/renpy.py"
    test -f "${framework}/base/renpy/common/00start.rpy"
done
rm -f "${SOURCE_DIR}/renpy/vc_version.py"

xcodebuild -create-xcframework \
    -framework "${DEVICE_FRAMEWORK}" \
    -framework "${SIMULATOR_FRAMEWORK}" \
    -output "${OUTPUT}"

test -f "${OUTPUT}/Info.plist"
if command -v ccache >/dev/null 2>&1; then
    ccache --show-stats --verbose
fi
echo "Created ${OUTPUT}"
