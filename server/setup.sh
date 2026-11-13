#!/bin/sh
# Idempotent setup of the home listener on the parent's Mac. Safe to run again.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
data="$HOME/Library/Application Support/unicorn-listener"
model=ggml-small.bin   # keep in step with MODEL in listener.py
mkdir -p "$data"

command -v whisper-server >/dev/null || brew install whisper.cpp   # formula was "whisper-cpp" before

fetch() {  # url file sha256
  if [ ! -f "$data/$2" ] || ! shasum -a 256 "$data/$2" | grep -q "^$3"; then
    echo "downloading $2"
    curl -fL --progress-bar -o "$data/$2.part" "$1"
    shasum -a 256 "$data/$2.part" | grep -q "^$3" || { echo "checksum mismatch for $2"; rm -f "$data/$2.part"; exit 1; }
    mv "$data/$2.part" "$data/$2"
  fi
}
fetch "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$model" "$model" \
  1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b
fetch https://storage.googleapis.com/mediapipe-models/audio_classifier/yamnet/float32/1/yamnet.tflite yamnet.tflite \
  4d8b4a53282dc83ef04e3e7dbc4fbc98082e34e44ed798e16c3a0cdd4c584faf

# numpy + LiteRT for the TV guess, in a venv of its own so launchd starts without the network
if [ ! -x "$data/venv/bin/python" ] && command -v uv >/dev/null; then
  uv venv --quiet --python 3.13 "$data/venv"
  uv pip install --quiet --python "$data/venv/bin/python" numpy ai-edge-litert
fi

python3 "$here/listener.py" --setup   # token + defaults into ~/.config/unicorn-pet/personal.json, mode 600
echo "ok. run it: $here/run.sh   (or install the login agent: $here/install-launchd.sh)"
