#!/bin/bash
# Anthill r6 vs r5 vs Forts calibration: 4 copies of one AI per game, 25k ticks.
set -u
ROOT=/Users/bradley/glob2/.claude/worktrees/ant-hill-terrain-revamp-36bb55
C=$ROOT/artifacts/anthill/calib
G=$C/glob2-r6
TICKS=${TICKS:-25000}
JOBS=${JOBS:-4}
run() {
  map=$1 ai=$2 out=$C/games/$1-$2
  [ -f $out/result.json ] && return
  rm -rf $out
  SDL_VIDEODRIVER=dummy $G game run --map-file $C/maps/$map/map-r0.map.gz --game-seed 1 \
    --player $ai --player $ai --player $ai --player $ai --ticks $TICKS --compute-threads 2 \
    --telemetry team-timeline --save final --output-dir $out > $C/games/$map-$ai.log 2>&1
  echo "done $map $ai exit $?"
}
export -f run; export G C TICKS
mkdir -p $C/games
for m in ${MAPS:-r6-101 r5-101 forts-101 r6-202 r5-202 forts-202}; do
  for ai in nicowar cortex cabino maxima; do echo "$m $ai"; done
done | xargs -P $JOBS -n 2 bash -c 'run "$0" "$1"'
echo ALL DONE
