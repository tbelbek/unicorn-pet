#!/bin/sh
# Build the game core to emu/unicorn.wasm (Homebrew llvm + lld, no emscripten).
set -e
cd "$(dirname "$0")"
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${LLD:-/opt/homebrew/opt/lld/bin}
"$LLVM/clang++" --target=wasm32 -std=c++17 -O2 -nostdlib -ffreestanding \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -mbulk-memory \
  -fuse-ld="$LLD/wasm-ld" -Wl,--no-entry -Wl,-z,stack-size=65536 \
  -o unicorn.wasm wasm_main.cpp
echo "built emu/unicorn.wasm ($(wc -c < unicorn.wasm | tr -d ' ') bytes)"
