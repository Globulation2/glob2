#!/bin/bash
# Fetch Neurotica replays off therig and play them locally.
#
# Replays are self-contained (map + orders), so playback needs no policy
# server and no GPU. The engine resolves -replay through its file manager
# rather than as a filesystem path, so replays must live in
# ~/.glob2/replays/ and are named without the .replay extension.
#
# Usage:
#   watch.sh fetch          # copy new replays down and list them
#   watch.sh list           # list what is already local
#   watch.sh play <name>    # play one (name with or without .replay)
set -eu
GLOB2=${GLOB2:-$(cd "$(dirname "$0")/../.." && pwd)/build/src/glob2}
DEST=$HOME/.glob2/replays
mkdir -p "$DEST"

case "${1:-fetch}" in
  fetch)
    scp -o ConnectTimeout=20 'therig.local:~/neurotica/replays/*.replay' "$DEST"/ 2>/dev/null \
      || { echo "nothing to fetch (no replays on therig yet)"; exit 0; }
    echo "--- available (W = Neurotica won, LOSS = opponent won, CAP = hit tick cap) ---"
    ls -1 "$DEST" | sed 's/\.replay$//'
    ;;
  list)
    ls -1 "$DEST" | sed 's/\.replay$//'
    ;;
  play)
    NAME=$(basename "${2:?usage: watch.sh play <replay-name>}" .replay)
    [ -f "$DEST/$NAME.replay" ] || { echo "no such replay: $NAME" >&2; exit 1; }
    exec "$GLOB2" -replay "$NAME"
    ;;
  *)
    echo "usage: watch.sh [fetch|list|play <name>]" >&2; exit 1;;
esac
