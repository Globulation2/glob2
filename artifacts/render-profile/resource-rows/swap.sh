#!/bin/zsh
set -eu
out="$PWD/artifacts/render-profile/resource-rows"
binary="${GLOB2_ROWS_BINARY:-$PWD/build/darwin/client/debug/dev-dev_fast-true-linker-auto/test/torus-render-benchmark}"
export GLOB2_USER_DATA_DIR="$out/swap-profile"
mkdir -p "$GLOB2_USER_DATA_DIR"
printf 'highResolutionArtwork=0\n' >"$GLOB2_USER_DATA_DIR/preferences.txt"
export GLOB2_BENCH_FLAT=1 GLOB2_BENCH_SIZE=512x64 GLOB2_BENCH_UNITS=0 GLOB2_BENCH_SPARSE_TERRAIN=1
export GLOB2_BENCH_MODE='2D no clouds' GLOB2_BENCH_ZOOM=1 GLOB2_BENCH_CAMERA_MOTION=1
export GLOB2_BENCH_WARMUP_FRAMES=12 GLOB2_BENCH_FRAMES=80 GLOB2_BENCH_FRAME_TIMES=1
export GLOB2_BENCH_PAUSE_PRESENTATION=1 GLOB2_DIRECT_TERRAIN=1
for enabled in 0 1; do
  export GLOB2_SWAP_FULL_GL_FRAMES="$enabled"
  name="swap-4k-$enabled"
  GLOB2_BENCH_CAPTURE="$out/$name.ppm" "$binary" --renderer programmable-gl --no-fullscreen --mute \
    --window-size 3840x2160 >"$out/$name.log" 2>&1
  rg -q '^BACKEND .*active=programmable-gl ' "$out/$name.log"
  rg 'median=|p99=|^simulation_checksum=' "$out/$name.log"
done
cmp "$out/swap-4k-0.ppm" "$out/swap-4k-1.ppm"
