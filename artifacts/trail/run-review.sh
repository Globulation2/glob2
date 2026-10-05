#!/usr/bin/env bash
set -euo pipefail
root=/home/bradley/.codex/worktrees/6eeb/glob2
cd "$root"
for phase in before after; do
 build_dir=build/linux/client/release
 source_root="$root"
 if [[ $phase == before ]]; then
  build_dir=artifacts/trail/baseline-source/build/linux/client/release
  source_root="$root/artifacts/trail/baseline-source"
 fi
 env -u DISPLAY GLOB2_TEST_SOURCE_ROOT="$source_root" LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 \
  python3 test/run_tests.py --build-dir "$build_dir" --binary engine --filter 'TrailReview/*' \
  --jobs 1 --display-jobs 1 --timeout 180 --artifacts "artifacts/trail/$phase" --junit "artifacts/trail/$phase/junit.xml"
done
