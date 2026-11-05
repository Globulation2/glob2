#!/usr/bin/env bash
set -eo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
label="$1"
opts=(); hd=1; zoom=0.85; frames=60; warmup=60
case "$label" in
 far-hd) zoom=0.35; warmup=240 ;;
 near-hd) warmup=240 ;;
 near-native) hd=0; warmup=240 ;;
 motion-hd) zoom=0.30; frames=240; warmup=120; opts=(GLOB2_BENCH_CAMERA_MOTION=1) ;;
 *) exit 2 ;;
esac
out="$dir/$label"; mkdir -p "$out/profile"
printf 'highResolutionArtwork=%s\nuiScale=0\n' "$hd" > "$out/profile/preferences.txt"
env GLOB2_USER_DATA_DIR="$out/profile" GLOB2_USER_DIR="$out/profile" \
 GLOB2_BENCH_GAME="$dir/honeycomb.game.gz" \
 GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_FRAMES="$frames" \
 GLOB2_BENCH_WARMUP_FRAMES="$warmup" GLOB2_BENCH_FRAME_TIMES=1 GLOB2_BENCH_ZOOM="$zoom" \
 GLOB2_BENCH_PAN_X=145 GLOB2_BENCH_PAN_Y=190 GLOB2_BENCH_ADAPTIVE_ZOOM=1 \
 GLOB2_BENCH_CAPTURE="$out/frame.ppm" "${opts[@]}" "${GLOB2_BENCH_BINARY:-$dir/torus-render-benchmark}" \
 --renderer legacy-gl --window-size 1280x800 --no-fullscreen --mute > "$out/trace.log" 2>&1
