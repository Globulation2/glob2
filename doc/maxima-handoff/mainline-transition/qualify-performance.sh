#!/bin/bash
set -euo pipefail
cd /home/bradley/glob2-maxima-mainline-0e9092a79
result=$(docker wait maxima-mainline-build-0e9092a79)
test "$result" = 0
python3 tools/maxima_win_experiment.py --prepare --binary build-portable/src/glob2 --output output/mainline-linux-qualification
nice -n 10 taskset -c 31 python3 tools/qualify_maxima_no_orders.py output/mainline-linux-qualification --output output/mainline-linux-qualification/no-orders
nice -n 10 python3 benchmark.py
