#!/bin/bash
set -euo pipefail
export LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib
python3 docs/.work/run_with_shared_host_cpuset.py --audit artifacts/resource-growth/remaining/cpuset-final-integration.json --cpus 0-3 --reserve-cpus 0-3,16-19 --timeout-seconds 7200 -- python3 test/run_with_benchmark_governor.py --audit artifacts/resource-growth/remaining/governor-final-integration.json --cpus 0 1 2 3 -- python3 docs/.work/run-growth-final-pairs.py artifacts/resource-growth/remaining/final-integration-plan.json > artifacts/resource-growth/remaining/final-integration-timing.log 2>&1
