#!/bin/sh
# Build the full 16-UF2 "Schickacka R1" Pico 1 (RP2040) release matrix:
#   Boards : PU18, PU8
#   Clocks : stock (OFF), OC (ON)
#   Regions: SCEA (0), SCEE (1), SCEI (2), Generic (3)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RELEASE_TAG="${1:-Release_1.1}"
BUILD="$ROOT/tools/build-src"
OUT_ROOT="${2:-$ROOT/releases/$RELEASE_TAG}"
SUBQ_CRC="${3:-OFF}"
MENU_BIN="${4:-picomenu/picostation-menu.bin}"
WARN_LOG="$BUILD/matrix_warnings.log"

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT/PU18" "$OUT_ROOT/PU8" "$BUILD"
: > "$WARN_LOG"

region_id() {
    case "$1" in
        SCEA) echo 0 ;;
        SCEE) echo 1 ;;
        SCEI) echo 2 ;;
        Generic) echo 3 ;;
        *) echo "Unknown region $1" >&2; exit 1 ;;
    esac
}

for BOARD in PU18 PU8; do
    for OC in OFF ON; do
        OC_TAG=""
        [ "$OC" = "ON" ] && OC_TAG="_OC"
        for REG in SCEA SCEE SCEI Generic; do
            RID=$(region_id "$REG")
            OUT_NAME="picostation_${RELEASE_TAG}_${BOARD}${OC_TAG}_${REG}.uf2"
            echo "=== Building $OUT_NAME (MAINBOARD=$BOARD, OC=$OC, REGION=$RID, SUBQ_CRC=$SUBQ_CRC) ==="
            cmake -S "$ROOT" -B "$BUILD" -G Ninja \
                -DPICO_SDK_PATH="$ROOT/tools/pico-sdk" \
                -DPICO_BOARD=pico \
                -DPICOSTATION_VARIANT=picostation_pico1 \
                -DPICOSTATION_MAINBOARD="$BOARD" \
                -DPICOSTATION_OVERCLOCK="$OC" \
                -DPICOSTATION_SCEX_REGION="$RID" \
                -DPICOSTATION_SUBQ_CRC="$SUBQ_CRC" \
                -DPICOSTATION_SCEX_OPEN_DRAIN=OFF \
                -DPICOSTATION_MENU_BIN="$MENU_BIN" \
                -DCMAKE_BUILD_TYPE=Release >/dev/null
            ninja -C "$BUILD" 2>&1 | tee -a "$WARN_LOG"
            cp "$BUILD/picostation_pico1.uf2" "$OUT_ROOT/$BOARD/$OUT_NAME"
        done
    done
done

# Restore default build-src configuration (PU18, non-OC, Generic)
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DPICO_SDK_PATH="$ROOT/tools/pico-sdk" \
    -DPICO_BOARD=pico \
    -DPICOSTATION_VARIANT=picostation_pico1 \
    -DPICOSTATION_MAINBOARD=PU18 \
    -DPICOSTATION_OVERCLOCK=OFF \
    -DPICOSTATION_SCEX_REGION=3 \
    -DPICOSTATION_SUBQ_CRC=OFF \
    -DPICOSTATION_SCEX_OPEN_DRAIN=OFF \
    -DPICOSTATION_MENU_BIN="picomenu/picostation-menu.bin" \
    -DCMAKE_BUILD_TYPE=Release >/dev/null
ninja -C "$BUILD" >/dev/null

echo "=== COMPILER WARNINGS COUNT ==="
grep -ic "warning:" "$WARN_LOG" || true
rm -f "$WARN_LOG"

echo "=== GENERATED UF2s ==="
ls -lh "$OUT_ROOT/PU18" "$OUT_ROOT/PU8"
