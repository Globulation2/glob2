#!/bin/bash
cd "$(dirname "$0")/../.."; R=$PWD/artifacts/bench; mkdir -p $R/captures
cap() { name=$1 bin=$2 assets=$3
  env ${assets:+GLOB2_ASSET_DIR=$assets} GLOB2_USER_DATA_DIR=$R/profile GLOB2_BENCH_GAME=$R/game/final.game.gz GLOB2_BENCH_FLAT=1 GLOB2_BENCH_ZOOM=1 GLOB2_BENCH_PAN_X=47 GLOB2_BENCH_PAN_Y=52 GLOB2_BENCH_FRAMES=20 GLOB2_BENCH_WARMUP_FRAMES=20 "GLOB2_BENCH_MODE=2D no clouds" GLOB2_BENCH_PAUSE_PRESENTATION=1 GLOB2_BENCH_CAPTURE=$R/captures/$name.ppm GLOB2_RENDER_EXECUTION=immediate $bin --renderer legacy-gl --no-fullscreen --mute --window-size 1280x800 > $R/captures/$name.log 2>&1
  echo "$name exit $?"; }
cap base1L artifacts/bin-base/torus-render-benchmark ""
cap cand4Ls4 artifacts/bin-cand/torus-render-benchmark $PWD/artifacts/lava4-rt-s4
