#!/bin/bash
set -euo pipefail
export LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib
python3 docs/.work/run_with_shared_host_cpuset.py --audit artifacts/resource-growth/copy-policy/cpuset.json --cpus 0-3 --reserve-cpus 0-3,16-19 --timeout-seconds 1800 -- python3 test/run_with_benchmark_governor.py --audit artifacts/resource-growth/copy-policy/governor.json --cpus 0 1 2 3 -- python3 docs/.work/run-growth-copy-policy.py
