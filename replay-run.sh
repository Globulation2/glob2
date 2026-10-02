#!/bin/bash
# Usage: replay-run.sh <label>: plays back the label's run2 replay with a checksum sidecar.
set -u
label=$1
root=$(pwd)
out=$root/artifacts/m0/$label/playback
rm -rf "$out"; mkdir -p "$out/profile"
cp "$root/artifacts/m0/$label/run4/game.replay" "$out/r.replay"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy GLOB2_USER_DATA_DIR=$out/profile GLOB2_CHECKSUM_SIDECAR=1 GLOB2_CHECKSUM_SIDECAR_MAX_TICKS=6000 \
  perl -e "alarm 320; exec @ARGV" "$root/build/darwin/client/release/src/glob2" -replay "$out/r.replay" > "$out/log" 2>&1
echo "playback exit $?"
