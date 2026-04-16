#!/bin/bash

set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
OUTPUT_PATH="$ROOT_DIR/comfy_aimdo/aimdo.so"
FUNCHOOK_VERSION=1.1.3
FUNCHOOK_SRC="$BUILD_DIR/funchook-$FUNCHOOK_VERSION"
FUNCHOOK_BUILD_DIR="$BUILD_DIR/funchook-$FUNCHOOK_VERSION-distorm"
FUNCHOOK_TARBALL="$BUILD_DIR/funchook-$FUNCHOOK_VERSION.tar.gz"
CUDA_INCLUDE_DIR=/usr/local/cuda-12.1/include
CUDA_STUB_DIR=/usr/local/cuda-12.1/targets/x86_64-linux/lib/stubs

if [ ! -f "$FUNCHOOK_SRC/CMakeLists.txt" ]; then
    URL="https://github.com/kubo/funchook/releases/download/v$FUNCHOOK_VERSION/funchook-$FUNCHOOK_VERSION.tar.gz"

    mkdir -p "$BUILD_DIR"
    curl -fL "$URL" -o "$FUNCHOOK_TARBALL"
    rm -rf "$FUNCHOOK_SRC"
    tar -xzf "$FUNCHOOK_TARBALL" -C "$BUILD_DIR"
fi

if [ ! -f "$FUNCHOOK_BUILD_DIR/libfunchook.a" ] || [ ! -f "$FUNCHOOK_BUILD_DIR/libdistorm.a" ]; then
    if ! command -v cmake >/dev/null 2>&1; then
        echo "cmake is required to build funchook" >&2
        exit 1
    fi

    mkdir -p "$FUNCHOOK_BUILD_DIR"

    cmake -S "$FUNCHOOK_SRC" -B "$FUNCHOOK_BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DFUNCHOOK_BUILD_SHARED=OFF \
        -DFUNCHOOK_BUILD_STATIC=ON \
        -DFUNCHOOK_BUILD_TESTS=OFF \
        -DFUNCHOOK_DISASM=distorm \
        -DFUNCHOOK_INSTALL=OFF

    cmake --build "$FUNCHOOK_BUILD_DIR" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
fi

mkdir -p "$(dirname -- "$OUTPUT_PATH")"

# shellcheck disable=SC2086
gcc -shared -o "$OUTPUT_PATH" -fPIC -O2 -g -pthread \
    ${AIMDO_EXTRA_CFLAGS:-} \
    "$ROOT_DIR"/src/*.c "$ROOT_DIR"/src-posix/*.c \
    -I"$ROOT_DIR/src" -I"$FUNCHOOK_SRC/include" -I"$CUDA_INCLUDE_DIR" \
    -L"$CUDA_STUB_DIR" \
    "$FUNCHOOK_BUILD_DIR/libfunchook.a" "$FUNCHOOK_BUILD_DIR/libdistorm.a" \
    -lcuda -ldl
