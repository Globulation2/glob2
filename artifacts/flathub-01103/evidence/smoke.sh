#!/bin/sh
set -eu
flatpak info --user org.globulation2.Globulation2
flatpak run --user --command=sh org.globulation2.Globulation2 -c '
set -eu
mkdir -p /var/data/validation-01103
glob2 info version
glob2 info sim-version --format json
glob2 map generate river --seed 713 --width 128 --height 128 --teams 2 --output /var/data/validation-01103/river.map --preview /var/data/validation-01103/river.png --report-file /var/data/validation-01103/river.json
glob2 game run --map-file /var/data/validation-01103/river.map.gz --game-seed 713 --player castor --player cortex --compute-threads 2 --ticks 2048 --write-replay --telemetry checksums --save final --output-dir /var/data/validation-01103/game
glob2 game run --load-game /var/data/validation-01103/game/final.game.gz --compute-threads 2 --ticks 2112 --telemetry checksums --output-dir /var/data/validation-01103/continued
'
