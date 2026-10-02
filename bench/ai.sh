#!/bin/bash
mode=$1; ai=$2; dir=/tmp/bench/ai-$mode-$ai; rm -rf $dir; mkdir -p $dir; printf "gameSpeed=10\n" > $dir/preferences.txt
env=""; [ $mode = serial ] && env="GLOB2_SIM_THREAD=0"
start=$(python3 -c 'import time;print(time.time())')
env $env GLOB2_USER_DATA_DIR=$dir GLOB2_TEST_SEED=5 GLOB2_TEST_MAX_TICKS=3000 taskpolicy -c background ./build/darwin/client/release/src/glob2 -test-games 1 --map balanced --matchup maxima,cortex,maxima,cortex -m -s 1024x768 --ai-threads $ai > $dir/log 2>&1
end=$(python3 -c 'import time;print(time.time())')
echo "$mode ai=$ai wall=$(python3 -c "print(round($end-$start,1))")s $(grep -o 'ended: [0-9]* ticks' $dir/log)"
