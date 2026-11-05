#!/usr/bin/env bash
set -eo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
out="$dir/$1"
mkdir -p "$out/profile"
hd=1; zoom=5; pan=1
case "$1" in
  close-pan) ;;
  native-start) hd=0; zoom=1; pan=0 ;;
  hd-start) zoom=1; pan=0 ;;
  zoom-pan) zoom=.3; pan=0 ;;
  *) exit 2 ;;
esac
opts=()
if [[ "$pan" = 1 ]]; then opts+=(GLOB2_BENCH_CAMERA_PAN=1); fi
if [[ "$1" = zoom-pan ]]; then opts+=(GLOB2_BENCH_CAMERA_MOTION=1); fi
printf 'highResolutionArtwork=%s\nuiScale=0\n' "$hd" > "$out/profile/preferences.txt"
env GLOB2_USER_DATA_DIR="$out/profile" GLOB2_USER_DIR="$out/profile" \
  GLOB2_BENCH_GAME="$dir/honeycomb.game.gz" \
  GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_FRAMES=240 \
  GLOB2_BENCH_WARMUP_FRAMES=1 GLOB2_BENCH_FRAME_TIMES=1 GLOB2_BENCH_ZOOM="$zoom" \
  GLOB2_BENCH_PAN_X=145 GLOB2_BENCH_PAN_Y=190 GLOB2_BENCH_ADAPTIVE_ZOOM=1 \
  GLOB2_BENCH_CAPTURE="$out/frame.ppm" "${opts[@]}" \
  "${GLOB2_BENCH_BINARY:-$PWD/build/hd-pan-release/test/torus-render-benchmark}" \
  --renderer legacy-gl --window-size 1920x1080 --no-fullscreen --mute > "$out/trace.log" 2>&1
