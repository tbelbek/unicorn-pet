# Host tests for the core, with ASan + UBSan so any out-of-bounds draw fails loudly.
CXX ?= clang++
CXXFLAGS = -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all
SRC = test_core.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp ../core/qr.cpp

test: out
	$(CXX) $(CXXFLAGS) $(SRC) -o out/test_core
	cd . && ./out/test_core
	@if command -v sips >/dev/null; then \
		for f in out/*.ppm; do sips -s format png $$f --out $${f%.ppm}.png >/dev/null && rm -f $$f || exit 1; done; \
	else echo "no sips (macOS only): screenshots stay as out/*.ppm"; fi

out:
	mkdir -p out

# A five-year-old plays the real core for days, across many seeds; every loop is checked (monkey.cpp).
monkey: out
	$(CXX) $(CXXFLAGS) monkey.cpp ../core/game.cpp ../core/render.cpp ../core/sound.cpp ../core/qr.cpp -o out/monkey
	./out/monkey

# The firmware's time-zone helpers (fw/src/tz.h), with real POSIX summer-time rules.
tz: out
	$(CXX) $(CXXFLAGS) test_tz.cpp -o out/test_tz
	./out/test_tz

# The firmware's release-manifest parser (fw/src/manifest.h), on the host.
ota: out
	$(CXX) $(CXXFLAGS) test_ota.cpp -o out/test_ota
	./out/test_ota

# The firmware's listener reply parser (fw/src/http_reply.h), on the host.
http: out
	$(CXX) $(CXXFLAGS) test_http.cpp -o out/test_http
	./out/test_http

# tools/release.py signs and checks a dummy image with a throwaway key; tampering must fail.
release:
	cd .. && uv run --quiet --with cryptography python tools/release.py --selftest

# Every language complete, and core/strings.gen.h generated from tools/i18n.py
i18n:
	cd .. && python3 tools/export_i18n.py --check

# The phone setup page is embedded in the firmware as portal.gen.h: it must match portal.html.
portal:
	cd .. && python3 tools/embed_portal.py --check

assets:
	cd .. && uv run --quiet --with pillow python tools/export_c.py --check

# Her voice packs (Opus): the generated headers match tools/voice.py and tools/routines.py, and ffmpeg decodes
# every clip of fw/data/voice.bin (and of the private packs in fw/personal/, when they are there). No network.
voice:
	cd .. && python3 tools/voice.py --check

# The emulator's page script must at least parse (a duplicate name once broke it silently).
emu: | out
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

# The home listener's pure-Python parts (yes/no rules, WAV + voice checks, HTTP with a fake Whisper).
listener:
	cd ../server && python3 -W ignore::ResourceWarning -m unittest -q test_listener

all: test monkey tz ota http release i18n portal assets voice emu wasm listener

.PHONY: test monkey tz ota http release i18n portal assets emu wasm listener all record tour
