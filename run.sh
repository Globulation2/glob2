#!/usr/bin/env bash
set -euo pipefail
binary="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
save="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
output="$(cd "$(dirname "$3")" && pwd)/$(basename "$3")"
route="$4"
mkdir -p "$output/profile"
printf 'highResolutionArtwork=1\nuiScale=0\n' > "$output/profile/preferences.txt"
case "$route" in
    fit) options=(GLOB2_BENCH_FULL_MAP=1) ;;
    motion) options=(GLOB2_BENCH_ZOOM=0.1 GLOB2_BENCH_CAMERA_MOTION=1) ;;
    *) exit 2 ;;
esac
env GLOB2_USER_DATA_DIR="$output/profile" GLOB2_USER_DIR="$output/profile" \
    GLOB2_BENCH_GAME="$save" GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MODE='2D clouds' \
    GLOB2_BENCH_FRAMES=48 GLOB2_BENCH_WARMUP_FRAMES=3 GLOB2_BENCH_FRAME_TIMES=1 \
    GLOB2_BENCH_ADAPTIVE_ZOOM=1 GLOB2_BENCH_CAPTURE="$output/frame.ppm" \
    "${options[@]}" "$binary" --renderer legacy-gl --window-size 1280x800 \
    --no-fullscreen --mute > "$output/trace.log" 2>&1
