#!/usr/bin/env bash
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/build"}
UE4SS_SRC=${UE4SS_SRC:?Set UE4SS_SRC to the patched UE4SS source tree}
UE4SS_BUILD=${UE4SS_BUILD:?Set UE4SS_BUILD to the matching UE4SS CMake build directory}
UE4SS_LIB=${UE4SS_LIB:?Set UE4SS_LIB to the matching libUE4SS.so}

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja \
  -DUE4SS_SRC="$UE4SS_SRC" \
  -DUE4SS_BUILD="$UE4SS_BUILD" \
  -DUE4SS_LIB="$UE4SS_LIB"
cmake --build "$BUILD_DIR" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"

printf 'Built: %s\n' "$BUILD_DIR/PalworldServerAutoReviveNative.so"
