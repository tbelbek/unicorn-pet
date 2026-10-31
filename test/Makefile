# Host tests for the core, with ASan + UBSan so any out-of-bounds draw fails loudly.
CXX ?= clang++
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all
SRC = test_core.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp

test: out
	$(CXX) $(CXXFLAGS) $(SRC) -o out/test_core
	cd . && ./out/test_core
	@for f in out/*.ppm; do sips -s format png $$f --out $${f%.ppm}.png >/dev/null 2>&1; done; rm -f out/*.ppm

out:
	mkdir -p out

# Same events through the WebAssembly build the browser emulator uses.
wasm:
	../emu/build.sh
	node test_wasm.mjs

# Scripted play-through from the real core -> design/gameplay.gif
record: out
	mkdir -p out/rec && rm -f out/rec/*.ppm out/rec/times.txt
	$(CXX) -std=c++17 -O2 record.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp -o out/record
	./out/record
	cd .. && uv run --quiet --with pillow python tools/make_gif.py

all: test wasm

.PHONY: test wasm all record
