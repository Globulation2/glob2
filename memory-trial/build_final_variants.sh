#!/usr/bin/env bash
set -euo pipefail
python3 - <<'PY'
import pathlib,time,json
for name in ['cpuset-assets.json','cpuset-heap.json']:
 p=pathlib.Path('artifacts/memory-wins')/name
 while True:
  if p.exists():
   try:s=json.loads(p.read_text())
   except json.JSONDecodeError:time.sleep(1);continue
   if s['state']=='failed':raise RuntimeError(s)
   if s['state']=='completed':
    assert s['exit_code']==0 and s['cgroup_restored'] and s['wrapper_affinity_restored'],s
    break
  time.sleep(1)
PY
test -z "$(git status --porcelain)"
mkdir -p artifacts/memory-wins/independent-trial
cp artifacts/memory-wins/asset-comparison.json artifacts/memory-wins/independent-trial/asset-comparison.json
cp artifacts/memory-wins/environment.json artifacts/memory-wins/independent-trial/environment.json
cp -a build/linux/client/release/client-full-direct-1111/runtime-assets artifacts/memory-wins/independent-trial/runtime-assets
export GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix
export CCACHE=1
git checkout --detach 50d4372f0
scons release=1 client_profile=full server=0 CXX="$PWD/artifacts/memory-wins/profile-cxx" CXXFLAGS=-g1 LINKFLAGS= -j12 build/linux/client/release/client-full-direct-1111/src/glob2 asset-loading-benchmark torus-render-benchmark unit-tests engine-tests > artifacts/memory-wins/final-baseline-build.log 2>&1
python3 artifacts/memory-wins/preserve.py final-baseline
git checkout codex/hd-memory-trial
scons release=1 client_profile=full server=0 CXX="$PWD/artifacts/memory-wins/profile-cxx" CXXFLAGS=-g1 LINKFLAGS= -j12 build/linux/client/release/client-full-direct-1111/src/glob2 asset-loading-benchmark torus-render-benchmark unit-tests engine-tests > artifacts/memory-wins/final-ab-build.log 2>&1
python3 artifacts/memory-wins/preserve.py final-ab
echo 'Final baseline and cleaned A+B release binaries preserved'
