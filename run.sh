#!/usr/bin/env bash
set -eo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
variant="$1"
zoom="$2"
suffix="${3:-}"
out="$dir/$variant-$zoom${suffix:+-$suffix}"
mkdir -p "$out/profile"
if [[ "$suffix" = pan ]]; then export GLOB2_BENCH_CAMERA_PAN=1; fi
printf 'highResolutionArtwork=1\nuiScale=0\n' > "$out/profile/preferences.txt"
binary="${GLOB2_BASELINE_BINARY:?Set GLOB2_BASELINE_BINARY to the baseline benchmark}"
if [[ "$variant" != baseline* ]]; then binary="${GLOB2_CANDIDATE_BINARY:?Set GLOB2_CANDIDATE_BINARY to the candidate benchmark}"; fi
env GLOB2_USER_DATA_DIR="$out/profile" GLOB2_USER_DIR="$out/profile" \
 GLOB2_BENCH_GAME="$dir/honeycomb.game.gz" \
 GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_FRAMES=180 \
 GLOB2_BENCH_WARMUP_FRAMES=120 GLOB2_BENCH_FRAME_TIMES=1 GLOB2_BENCH_ZOOM="$zoom" \
 GLOB2_BENCH_PAN_X=145 GLOB2_BENCH_PAN_Y=190 GLOB2_BENCH_ADAPTIVE_ZOOM=1 \
 GLOB2_BENCH_CAPTURE="$out/frame.ppm" "$binary" \
 --renderer legacy-gl --window-size 1280x800 --no-fullscreen --mute > "$out/trace.log" 2>&1
