#!/bin/sh
# Build the wasm core and serve the emulator at http://localhost:${PORT:-8732}/emu/
set -e
cd "$(dirname "$0")"
./build.sh
cd ..
echo "open http://localhost:${PORT:-8732}/emu/"
python3 -m http.server ${PORT:-8732} --bind 127.0.0.1
