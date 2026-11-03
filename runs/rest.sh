#!/bin/bash
set -u
cd "$(dirname "$0")/../.."
cond() { local b=$1; echo --condition base1L=artifacts/bin-base/$b --condition base4L=artifacts/bin-base/$b,artifacts/lava4-rt --condition cand4L=artifacts/bin-cand/$b,artifacts/lava4-rt --condition cand4Ls4=artifacts/bin-cand/$b,artifacts/lava4-rt-s4 --condition cand1L=artifacts/bin-cand/$b; }
python3 artifacts/bench/run.py --kind torus --tag torus-paused --paused --repeat 3 $(cond torus-render-benchmark) > artifacts/bench/runs/torus-paused.txt 2>&1
python3 artifacts/bench/run.py --kind software --tag software-animated --repeat 4 $(cond SoftwareRenderBenchmark) > artifacts/bench/runs/software-animated.txt 2>&1
python3 artifacts/bench/run.py --kind software --tag software-paused --paused --repeat 2 $(cond SoftwareRenderBenchmark) > artifacts/bench/runs/software-paused.txt 2>&1
echo DONE
