#!/bin/zsh
# Four-way paired runs: master, #1268, #1268 with grid water and one lava/ember layout ("lite"),
# and lite with HD deferral disabled ("lite_eager"). Fresh process and profile per run, rotating order.
A=/Users/bradley/glob2/.claude/worktrees/terrain-improvement-pr-review-b61172/artifacts/terrain-1268-measure
R=$A/runs-lite
mkdir -p $R
typeset -A TREE; TREE[master]=/Users/bradley/glob2/.claude/worktrees/terrain-improvement-pr-review-b61172
TREE[pr1268]=/Users/bradley/glob2/.claude/worktrees/pr1268-measure
TREE[lite]=/Users/bradley/glob2/.claude/worktrees/pr1268-lite; TREE[lite_eager]=${TREE[lite]}
run() { # name scenario map rep extra-env...
  local name=$1 sc=$2 map=$3 rep=$4; shift 4
  local prof="$R/profile-$name-$sc-$map-$rep"
  mkdir -p "$prof"; cp -r $A/profile/maps "$prof/"
  local extra=(); [[ $name == lite_eager ]] && extra=(GLOB2_BENCH_NO_DEFER=1)
  cd ${TREE[$name]}
  env GLOB2_USER_DATA_DIR="$prof" GLOB2_BENCH_UNITS=0 GLOB2_BENCH_FLAT=1 GLOB2_BENCH_MAP=maps/$map.map GLOB2_BENCH_MODE="2D no clouds" \
    GLOB2_BENCH_ZOOM=1 GLOB2_BENCH_HD_POLL_MS=2 GLOB2_BENCH_PACE_US=16667 GLOB2_BENCH_FRAMES=600 GLOB2_BENCH_WARMUP_FRAMES=2 GLOB2_BENCH_FRAME_TIMES=1 $extra "$@" \
    timeout 900 build/darwin/client/release/test/torus-render-benchmark --renderer gpu --no-fullscreen --mute --window-size 1280x800 \
    > $R/$name-$sc-$map-$rep.log 2>&1
  echo "$name $sc $map $rep exit=$?" >> $R/index.txt
  mv "$prof" "$R/.done-profile-$name-$sc-$map-$rep"
}
: > $R/index.txt
names=(master pr1268 lite lite_eager)
for rep in 1 2 3; do
  order=(${names[$rep,-1]} ${names[1,$rep-1]})
  for name in $order; do
    run $name static lava-shield-256 $rep
    run $name pan1 lava-shield-256 $rep GLOB2_BENCH_CAMERA_PAN=1 GLOB2_BENCH_PAN_SPEED=1
    run $name pan4 lava-shield-256 $rep GLOB2_BENCH_CAMERA_PAN=1 GLOB2_BENCH_PAN_SPEED=4
    run $name jump lava-shield-256 $rep GLOB2_BENCH_CAMERA_PAN=1 GLOB2_BENCH_JUMP_EVERY=120
    run $name pan4 bastion-keys-256 $rep GLOB2_BENCH_CAMERA_PAN=1 GLOB2_BENCH_PAN_SPEED=4
    run $name jump bastion-keys-256 $rep GLOB2_BENCH_CAMERA_PAN=1 GLOB2_BENCH_JUMP_EVERY=120
  done
done
echo DONE >> $R/index.txt
