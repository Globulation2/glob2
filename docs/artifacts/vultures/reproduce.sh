#!/bin/sh
# Run from the repository root. Each structured game requires a fresh output directory.
set -eu
vultures_root=$(pwd)
vultures_out="$vultures_root/artifacts/vultures-reproduced"
vultures_profile=$(mktemp -d /tmp/glob2-vultures.XXXXXX)
export GLOB2_USER_DIR="$vultures_profile"
mkdir -p "$vultures_out"
scons release=1 server=0 -j2 map-generator-golden-test map-generator-defaults-test build/src/glob2
build/src/MapGeneratorDefaultsTest glob2-vultures-reproduced-contracts
build/src/MapGeneratorGoldenTest glob2-vultures-reproduced-golden --require-rows
build/src/glob2 --generate-map vultures --seed 7 --width 128 --height 128 --teams 2 \
  --output "$vultures_out/duel-7.map" --preview "$vultures_out/duel-7.png" \
  --json "$vultures_out/duel-7.json"
build/src/glob2 --generate-map vultures --seed 7 --width 256 --height 256 --teams 4 \
  --output "$vultures_out/vultures-7.map" --preview "$vultures_out/vultures-7.png" \
  --json "$vultures_out/vultures-7.json"
build/src/glob2 --run-game --map-file "$vultures_out/duel-7.map" \
  --game-seed 19 --player nicowar --player nicowar --ticks 40000 \
  --save final --replay true --output-dir "$vultures_out/nicowar-full"
