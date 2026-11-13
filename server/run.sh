#!/bin/sh
# Start the listener in the foreground (launchd runs this too). Uses the venv from setup.sh when it is
# there (numpy + LiteRT for the TV guess); plain python3 works too, with a simpler TV guess.
here=$(cd "$(dirname "$0")" && pwd)
venv="$HOME/Library/Application Support/unicorn-listener/venv/bin/python"
[ -x "$venv" ] && exec "$venv" "$here/listener.py" "$@"
exec python3 "$here/listener.py" "$@"
