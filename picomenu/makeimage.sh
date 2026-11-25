#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MKPSXISO="$(command -v mkpsxiso || echo "$SCRIPT_DIR/../psx-toolchain/mkpsxiso-2.30-Linux/bin/mkpsxiso")"
"$MKPSXISO" -y "assets/xiso/isoconfig.xml" -o "build/picostation-menu.bin" -c "build/picostation-menu.cue"
echo "ISO image generated successfully."
