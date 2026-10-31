# Host tests for the core, with ASan + UBSan so any out-of-bounds draw fails loudly.
CXX ?= clang++
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all
SRC = test_core.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp ../core/qr.cpp

test: out
	$(CXX) $(CXXFLAGS) $(SRC) -o out/test_core
	cd . && ./out/test_core
	@for f in out/*.ppm; do sips -s format png $$f --out $${f%.ppm}.png >/dev/null 2>&1; done; rm -f out/*.ppm

out:
	mkdir -p out

# The firmware's time-zone helpers (fw/src/tz.h), with real POSIX summer-time rules.
tz: out
	$(CXX) $(CXXFLAGS) test_tz.cpp -o out/test_tz
	./out/test_tz

# The phone setup page is embedded in the firmware as portal.gen.h: it must match portal.html.
portal:
	cd .. && python3 tools/embed_portal.py --check

# The emulator's page script must at least parse (a duplicate name once broke it silently).
emu:
	python3 -c "import re,sys; s=open('../emu/index.html').read(); open('out/emu.mjs','w').write(re.search(r'<script type=\"module\">(.*?)</script>', s, re.S).group(1))"
	node --check out/emu.mjs

# Same events through the WebAssembly build the browser emulator uses.
wasm:
	../emu/build.sh
	node test_wasm.mjs

# Scripted play-through from the real core -> design/gameplay.gif
record: out
	mkdir -p out/rec && rm -f out/rec/*.ppm out/rec/times.txt
	$(CXX) -std=c++17 -O2 record.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp ../core/qr.cpp -o out/record
	./out/record
	cd .. && uv run --quiet --with pillow python tools/make_gif.py

# A captioned tour of every feature from the real core -> design/tour.gif
tour: out
	mkdir -p out/tour && rm -f out/tour/*.ppm out/tour/*.txt
	$(CXX) -std=c++17 -O2 tour.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp ../core/qr.cpp -o out/tour_bin
	./out/tour_bin
	cd .. && uv run --quiet --with pillow python tools/make_tour.py

all: test tz portal emu wasm

.PHONY: test tz portal emu wasm all record tour
