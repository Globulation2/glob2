#!/usr/bin/env bash
set -euo pipefail
root=$(pwd)
evidence="$root/artifacts/browser-fixes/merge-validation"
SDL_VIDEO_DRIVER=dummy python3 test/run_tests.py --build-dir build/browser-fix-merge-native --binary engine --filter 'ComputeExecutor/*' --filter 'HighResolutionIntegration/*' --filter 'SharedWorkerLifecycle/*' --filter 'WorldSnapshot/*' --filter 'RuntimeContinuation/*' --filter '*Save*/*' --filter 'Replay*/*' --filter 'SimVersion/*' --exclude-tag display -j4 --display-jobs 1 --junit "$evidence/native-engine.xml" --artifacts "$evidence/native-engine" > "$evidence/native-engine.log" 2>&1
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEO_DRIVER=x11 python3 test/run_tests.py --build-dir build/browser-fix-merge-native --binary engine --filter 'ComputeExecutor/*' --filter 'HighResolutionIntegration/*' --filter 'SharedWorkerLifecycle/*' --filter 'WorldSnapshot/*' --filter 'RuntimeContinuation/*' --filter '*Save*/*' --filter 'Replay*/*' --filter 'SimVersion/*' --tag display -j4 --display-jobs 1 --junit "$evidence/native-engine-display.xml" --artifacts "$evidence/native-engine-display" > "$evidence/native-engine-display.log" 2>&1
mkdir -p "$evidence/native-determinism"
GLOB2_REPLAY_PATH="$evidence/native-determinism/native.replay" GLOB2_CHECKSUM_SIDECAR=1 build/browser-fix-merge-native/src/glob2 game repeat games/cross-replay.game.gz --ticks 1500 --runs 1 > "$evidence/native-determinism/run.log" 2>&1
build/browser-fix-merge-native/src/glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr --map-file maps/FourSquares1.map.gz --output-dir "$evidence/native-determinism/verify-match" > "$evidence/native-determinism/verify-match.log" 2>&1
cmp test/fixtures/multiplayer/FourSquares1.verify-trace.txt "$evidence/native-determinism/verify-match/checksums.txt"
