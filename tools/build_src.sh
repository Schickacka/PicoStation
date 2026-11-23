#!/bin/sh
# Build the source tree for Pico 1 (RP2040) using the local tools/ checkout.
# Defaults to PICOSTATION_MAINBOARD=PU18 (primary deployment target); override
# via environment (e.g. PICOSTATION_MAINBOARD=PU8 ./tools/build_src.sh) or pass
# extra CMake -D arguments on the command line.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/tools/build-src"
MAINBOARD="${PICOSTATION_MAINBOARD:-PU18}"

if [ -f "$BUILD/CMakeCache.txt" ]; then
    CACHED_HOME=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt")
    if [ "$CACHED_HOME" != "$ROOT" ]; then
        rm -rf "$BUILD/CMakeCache.txt" "$BUILD/CMakeFiles"
    fi
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DPICO_SDK_PATH="$ROOT/tools/pico-sdk" \
    -DPICO_BOARD=pico \
    -DPICOSTATION_VARIANT=picostation_pico1 \
    -DPICOSTATION_MAINBOARD="$MAINBOARD" \
    -DCMAKE_BUILD_TYPE=Release \
    "$@"

ninja -C "$BUILD"

printf '\nUF2 (%s): %s\n' "$MAINBOARD" "$BUILD/picostation_pico1.uf2"
