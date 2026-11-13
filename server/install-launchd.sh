#!/bin/sh
# Install (or reinstall) the login agent that keeps the listener running. `--remove` takes it out again.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
dst="$HOME/Library/LaunchAgents/com.unicorn-pet.listener.plist"
launchctl bootout "gui/$(id -u)" "$dst" 2>/dev/null || true
if [ "${1:-}" = "--remove" ]; then rm -f "$dst"; echo "removed"; exit 0; fi
mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
sed -e "s|@RUN@|$here/run.sh|" -e "s|@LOG@|$HOME/Library/Logs/unicorn-listener.log|" \
  "$here/com.unicorn-pet.listener.plist" > "$dst"
launchctl bootstrap "gui/$(id -u)" "$dst"
echo "installed; log: ~/Library/Logs/unicorn-listener.log"
