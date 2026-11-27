#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

VENV="$DIR/.build_venv"
if [ ! -d "$VENV" ]; then
    python3 -m venv "$VENV"
fi

"$VENV/bin/pip" install --quiet --upgrade pip
"$VENV/bin/pip" install --quiet -r requirements.txt

"$VENV/bin/pyinstaller" \
    --noconfirm \
    --clean \
    --onefile \
    --windowed \
    --icon=assets/icon.ico \
    --add-data "assets/icon.png:assets" \
    --name PicoManager \
    app.py

chmod +x "$DIR/dist/PicoManager"
echo "Linux executable built: $DIR/dist/PicoManager ($(du -h "$DIR/dist/PicoManager" | cut -f1))"
