#!/bin/bash
# Usage: checksum-run.sh <label>   (run from repo root after building the client)
# Produces artifacts/m0/<label>/ with per-tick checksum sidecars, replays and result.json.
set -u
label=$1
bin=build/darwin/client/release/src/glob2
out=artifacts/m0/$label
rm -rf "$out"; mkdir -p "$out"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export GLOB2_USER_DATA_DIR=$PWD/$out/profile
mkdir -p "$GLOB2_USER_DATA_DIR"

# 1. Structured run: 4 AIs, multi-threaded AI compute, checksum telemetry, replay.
$bin --run-game --generator 15 --map-seed 42 --param teams=4 --param width=7 --param height=7 --game-seed 19 \
  --player nicowar --player warrush --player castor --player cortex \
  --ticks 6000 --compute-threads 3 --telemetry checksums --replay true \
  --output-dir "$PWD/$out/run4" > "$out/run4.log" 2>&1; echo "run4 exit $?"

# 2. Structured run: 2 AIs, single-threaded.
$bin --run-game --map-file $PWD/maps/A_big_pond.map.gz --game-seed 7 \
  --player econo --player numbi --player maxima --ticks 6000 --compute-threads 1 \
  --telemetry checksums --replay true \
  --output-dir "$PWD/$out/run2" > "$out/run2.log" 2>&1; echo "run2 exit $?"

# 3. Legacy -test-games-nox path (passive local player + AIs) with sidecar.
GLOB2_TEST_SEED=42 GLOB2_TEST_MAX_TICKS=5000 GLOB2_CHECKSUM_SIDECAR=1 \
GLOB2_REPLAY_PATH=$PWD/$out/testgames.replay \
  $bin -test-games-nox 1 --map A_big_pond --matchup nicowar,warrush,numbi > "$out/testgames.log" 2>&1
echo "testgames exit $?"
