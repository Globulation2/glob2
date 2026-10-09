#!/bin/bash
# usage: refusals.sh BIN W H TEAMS N  -> prints success count
cd /home/bradley/glob2-lava
bin=$1; w=$2; h=$3; teams=$4; n=$5
d=$(mktemp -d)
seq 1 $n | xargs -P 16 -I{} sh -c "$bin --generate-map lava-shield --seed {} --width $w --height $h --teams $teams --output $d/{}.map 2>&1 | grep -q '\[complete\]' && echo ok" | wc -l
rm -rf $d
