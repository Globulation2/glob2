#!/bin/zsh
# GPU-path captures (HD terrain, animation paused) of sea-lava-64 on master, #1268 and lite.
C=/Users/bradley/glob2/.claude/worktrees/terrain-improvement-pr-review-b61172/artifacts/terrain-1268-lite
P=/private/tmp/claude-501/-Users-bradley-glob2--claude-worktrees-terrain-improvement-pr-review-b61172/4b4358b2-8d2a-4a45-9d3c-6fd31731442a/scratchpad/cap
mkdir -p $P/ppm $C/captures
typeset -A TREE
TREE[master]=/Users/bradley/glob2/.claude/worktrees/terrain-improvement-pr-review-b61172
TREE[pr1268]=/Users/bradley/glob2/.claude/worktrees/pr1268-measure
TREE[lite]=/Users/bradley/glob2/.claude/worktrees/pr1268-lite
# tag zoom panX panY
SHOTS=("sea-z1 1 0 20" "lava-z1 1 30 20" "sea-z2 2 8 26" "lava-z2 2 38 26")
for name in master pr1268 lite; do
  for shot in $SHOTS; do
    set -- ${=shot}
    cd ${TREE[$name]}
    env GLOB2_USER_DATA_DIR=$P GLOB2_BENCH_UNITS=0 GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MAP=maps/sea-lava-64.map \
      GLOB2_BENCH_MODE="2D no clouds" GLOB2_BENCH_ZOOM=$2 GLOB2_BENCH_PAN_X=$3 GLOB2_BENCH_PAN_Y=$4 \
      GLOB2_BENCH_PAUSE_PRESENTATION=1 GLOB2_BENCH_FRAMES=120 GLOB2_BENCH_WARMUP_FRAMES=2 GLOB2_BENCH_HD_POLL_MS=50 \
      GLOB2_BENCH_CAPTURE=$P/ppm/$name-$1.ppm \
      timeout 300 build/darwin/client/release/test/torus-render-benchmark --renderer gpu --no-fullscreen --mute --window-size 1280x800 \
      > $C/captures/$name-$1.log 2>&1
    echo "$name $1 exit=$? $(grep MEASURE_HD $C/captures/$name-$1.log | tail -1 | cut -d' ' -f3-4)"
  done
done
