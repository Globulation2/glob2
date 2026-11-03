#!/bin/bash
cd "$(dirname "$0")/../.."
b=torus-render-benchmark
python3 artifacts/bench/run.py --kind torus --tag torus-animated-quiet --repeat 4 --condition base1L=artifacts/bin-base/$b --condition base4L=artifacts/bin-base/$b,artifacts/lava4-rt --condition cand4L=artifacts/bin-cand/$b,artifacts/lava4-rt --condition cand4Ls4=artifacts/bin-cand/$b,artifacts/lava4-rt-s4 --condition cand1L=artifacts/bin-cand/$b > artifacts/bench/runs/torus-animated-quiet.txt 2>&1
echo DONE
