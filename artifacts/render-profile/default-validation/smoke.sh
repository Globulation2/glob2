#!/bin/zsh
set -eu
out="$PWD/artifacts/render-profile/default-validation"
binary="$PWD/build/darwin/client/debug/dev-dev_fast-true-linker-auto/test/torus-render-benchmark"
export GLOB2_USER_DATA_DIR="$out/profile"
mkdir -p "$GLOB2_USER_DATA_DIR"
printf 'highResolutionArtwork=0\n' >"$GLOB2_USER_DATA_DIR/preferences.txt"
export GLOB2_BENCH_FLAT=1 GLOB2_BENCH_SIZE=512x256 GLOB2_BENCH_UNITS=0 GLOB2_BENCH_SPARSE_TERRAIN=1
export GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_ZOOM=1 GLOB2_BENCH_PAN_X=6 GLOB2_BENCH_PAN_Y=8
export GLOB2_BENCH_WARMUP_FRAMES=0 GLOB2_BENCH_FRAME_TIMES=1 GLOB2_BENCH_PAUSE_PRESENTATION=1
for count in 4 12; do
  GLOB2_BENCH_FOG=1 GLOB2_BENCH_DISCOVERY_CROSSING=1 GLOB2_BENCH_FRAMES="$count" GLOB2_BENCH_CAPTURE="$out/fog-$count.ppm" \
    "$binary" --renderer programmable-gl --no-fullscreen --mute --window-size 640x480 >"$out/fog-$count.log" 2>&1
  rg -q '^BACKEND .*active=programmable-gl ' "$out/fog-$count.log"
done
! cmp -s "$out/fog-4.ppm" "$out/fog-12.ppm"
rg -q '^FRAME_DIRECT_TERRAIN frame=11 draws=12 ' "$out/fog-12.log"
rg -q '^FRAME_DIRECT_DISCOVERY frame=11 rebuilt_cells=262144$' "$out/fog-12.log"

GLOB2_BENCH_SIZE=512x64 GLOB2_BENCH_RESOURCE_ROWS=1 GLOB2_BENCH_CAMERA_PAN=1 GLOB2_BENCH_WARMUP_FRAMES=12 GLOB2_BENCH_FRAMES=24 \
  "$binary" --renderer legacy-gl --no-fullscreen --mute --window-size 640x480 >"$out/rows.log" 2>&1
rg -q '^BACKEND .*active=legacy-gl ' "$out/rows.log"
rg -q '^FRAME_RESOURCE_ROWS frame=0 eligibility_cells=0 frame_cells=0 ' "$out/rows.log"
rg -q '^FRAME_DIRECT_TERRAIN frame=23 draws=36 ' "$out/rows.log"

GLOB2_BENCH_ARRAY_CHURN=1 GLOB2_PROFILE_GL_ARRAYS=1 \
  "$binary" --renderer legacy-gl --no-fullscreen --mute --window-size 640x480 >"$out/array.log" 2>&1
rg -q '^GL_ARRAY_CHURN cycle=20 promoted=16 sources=16$' "$out/array.log"

GLOB2_BENCH_SIZE=64x64 GLOB2_BENCH_TERRAIN_SWITCH=tiled GLOB2_BENCH_FRAMES=72 \
  "$binary" --renderer programmable-gl --no-fullscreen --mute --window-size 640x480 >"$out/tiled.log" 2>&1
rg -q '^FRAME_DIRECT_TERRAIN frame=67 draws=64 ' "$out/tiled.log"
rg -q '^FRAME_DIRECT_TERRAIN frame=71 draws=64 ' "$out/tiled.log"
rg -q '^FRAME_CACHE frame=71 ' "$out/tiled.log"
printf '%s\n' 'Default direct terrain, fog crossing, resource rows, array recycling and tiled fallback passed.'
