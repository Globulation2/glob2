#!/bin/sh
# run1.sh <arm> <bed> <seed> -- one headless game, results under /work/carol/hire/runs/<bed>/<arm>/<seed>
# arm: base|h1|h2|h3 (worktree g2-hire-base, g2-hire1, ...). bed: pond|pg|petri
set -u
arm=$1; bed=$2; seed=$3
case $arm in base) wt=g2-hire-base;; h1) wt=g2-hire1;; h2) wt=g2-hire2;; h3*) wt=g2-hire3; BIN=/work/carol/hire/bin/glob2-$arm;; *) wt=$arm;; esac
root=/work/carol-agent/REPOS/$wt
out=/work/carol/hire/runs/$bed/$arm/$seed
rm -rf "$out"; mkdir -p "$out/home"
case $bed in
  pond) args="--map-file maps/A_big_pond.map.gz --player econo --player econo --player econo --ticks ${TICKS:-90000}";;
  pg) args="--map-file maps/Playground.map.gz $(for i in 1 2 3 4 5 6 7 8; do printf -- '--player nicowar '; done) --ticks ${TICKS:-40000}";;
  petri) args="--load-game /work/carol/hire/petri2.map --ticks ${TICKS:-30000}";;
  pc) args="--load-game /work/carol/hire/petri_construction.map.gz --ticks ${TICKS:-20000}";;
esac
seedarg="--game-seed $seed"
case $bed in petri|pc) seedarg="";; esac
cd "$root" || exit 1
start=$(date +%s)
GLOB2_BENCH_RESEED=$seed HOME=$out/home SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy GLOB2_TEST_SEED=$seed \
  ${BIN:-./build/linux/client/release/src/glob2} --run-game --output-dir "$out" $args $seedarg \
  --telemetry team-timeline >"$out/stdout.log" 2>"$out/stderr.log"
echo "exit=$? secs=$(( $(date +%s) - start ))" >"$out/done"
rm -rf "$out/home" "$out/game.replay"
