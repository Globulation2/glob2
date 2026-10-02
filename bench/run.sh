#!/bin/bash
# usage: run.sh speed mode(serial|threaded) policy(normal|background) seed
speed=$1; mode=$2; policy=$3; seed=$4
dir=/tmp/bench/ud-$speed-$mode-$policy-$seed; rm -rf $dir; mkdir -p $dir
printf "gameSpeed=%s\n" $speed > $dir/preferences.txt
env=""; [ $mode = serial ] && env="GLOB2_SIM_THREAD=0"
pre=""; [ $policy = background ] && pre="taskpolicy -c background"
out=$(env $env GLOB2_USER_DATA_DIR=$dir GLOB2_TEST_SEED=$seed GLOB2_TEST_MAX_TICKS=3000 $pre ./build/darwin/client/release/src/glob2 -test-games 1 --map balanced --matchup maxima,cortex,maxima,cortex -m -s 1024x768 2>&1)
b=$(echo "$out" | grep "^BENCH" ); t=$(echo "$out" | grep -o "ended: [0-9]* ticks")
echo "speed=$speed mode=$mode policy=$policy seed=$seed $b $t"
