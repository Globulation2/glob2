#!/usr/bin/env bash
set -euo pipefail
cd /home/bradley/.codex/worktrees/colony-skins/glob2
unset DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR
export XDG_SESSION_TYPE=x11
export LD_LIBRARY_PATH="$PWD/artifacts/skins/sdk/lib"
validation=artifacts/skins/merge-validation/studio-gate
mkdir -p "$validation"
GLOB2_TEST_FFMPEG=/usr/bin/ffmpeg GLOB2_TEST_RECORD_GPU=1 python3 test/run_tests.py --binary engine --build-dir build/linux/client/release --jobs 4 --display-jobs 2 --filter 'LanMatchHarness/*' --filter 'WssTransport/*' --filter 'OnlineServices/*' --filter 'PlatformRoom/*' --filter 'OnlineScreenLifetime/*' --filter 'TurnEngineHarness/*' --filter 'EngineSession/*' --filter '*Skin*/*' --filter 'MapRenderResize/*' --filter 'GameplayRecording*/*' --filter '*Replay*/*' --filter 'SceneExtract/*' --filter 'Settings/*' --filter 'HiveMind*/*' --junit "$validation/local-gate-engine.xml" --artifacts "$validation/local-gate-engine" > "$validation/local-gate-engine.log" 2>&1
for setting in normal:1 overview:0.02 high:5; do
 name=${setting%:*}
 zoom=${setting#*:}
 profile="$PWD/$validation/local-gate-render-$name"
 mkdir -p "$profile"
 SDL_VIDEODRIVER=x11 GLOB2_USER_DATA_DIR="$profile" GLOB2_SKIN_PREVIEW_DIR="$PWD/artifacts/skins/units" SKIN_PREVIEW_SAVE="$PWD/artifacts/skins/initial.game.gz" SKIN_PREVIEW_CAPTURE=final.bmp SKIN_PREVIEW_BENCHMARK=crowd SKIN_BENCH_FRAMES=180 SKIN_BENCH_WARMUP=32 SKIN_PREVIEW_ZOOM="$zoom" xvfb-run -a build/linux/client/release/src/skin-game-preview -g -m -s800x600 > "$validation/local-gate-render-$name.log" 2>&1
done
NODE_OPTIONS=--import=/home/bradley/.npm/_npx/fd45a72a545557e9/node_modules/tsx/dist/loader.mjs GLOB2_PLATFORM_DIR="$PWD/platform" GLOB2_PLATFORM_DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55432/postgres python3 -m unittest discover -s tests/online -p test_platform_client.py -v > "$validation/local-gate-api.log" 2>&1
printf 'Native local gate passed\n'
