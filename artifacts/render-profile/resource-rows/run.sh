#!/bin/zsh
set -eu
out="$PWD/artifacts/render-profile/resource-rows"
binary="${GLOB2_ROWS_BINARY:-$PWD/build/darwin/client/debug/dev-dev_fast-true-linker-auto/test/torus-render-benchmark}"
export GLOB2_USER_DATA_DIR="$out/profile"
mkdir -p "$GLOB2_USER_DATA_DIR"
printf 'highResolutionArtwork=0\n' >"$GLOB2_USER_DATA_DIR/preferences.txt"
export GLOB2_BENCH_FLAT=1 GLOB2_BENCH_SIZE=512x64 GLOB2_BENCH_UNITS=0
export GLOB2_BENCH_SPARSE_TERRAIN=1 GLOB2_BENCH_RESOURCE_ROWS=1
export GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_ZOOM=1
export GLOB2_BENCH_WARMUP_FRAMES=12 GLOB2_BENCH_FRAMES=80 GLOB2_BENCH_FRAME_TIMES=1
export GLOB2_BENCH_PAUSE_PRESENTATION=1 GLOB2_DIRECT_TERRAIN=0
for camera in rest pan; do
  unset GLOB2_BENCH_CAMERA_PAN
  if [[ "$camera" == pan ]]; then export GLOB2_BENCH_CAMERA_PAN=1; fi
  for enabled in 0 1; do
    export GLOB2_RESOURCE_ROWS="$enabled"
    name="$camera-$enabled"
    GLOB2_BENCH_CAPTURE="$out/$name.ppm" "$binary" --renderer legacy-gl --no-fullscreen --mute \
      --window-size 640x480 >"$out/$name.log" 2>&1
    rg -q '^BACKEND .*active=legacy-gl ' "$out/$name.log"
    rg 'median=|p99=|^simulation_checksum=' "$out/$name.log"
  done
done
for camera in rest pan; do
  cmp "$out/$camera-0.ppm" "$out/$camera-1.ppm"
done
