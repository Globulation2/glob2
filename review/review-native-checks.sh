#!/usr/bin/env bash
set -euo pipefail
cd /home/bradley/.codex/worktrees/159d/glob2
build_dir=build/linux/client/debug/dev-dev_fast-true-linker-auto
binary="$build_dir/src/glob2"
python3 tools/tutorials/build.py --maps --binary "$binary" > artifacts/training/review-regenerate.log 2>&1
env -u SDL_VIDEO_DRIVER -u SDL_VIDEODRIVER python3 test/run_tests.py --build-dir "$build_dir" --filter 'TurnEngineHarness/the committed match record verifies*' --update-fixtures --junit artifacts/training/review-golden-update.xml > artifacts/training/review-golden-update.log 2>&1
python3 tools/tutorials/build.py --check --maps --binary "$binary" > artifacts/training/review-regeneration-check.log 2>&1
env -u SDL_VIDEO_DRIVER -u SDL_VIDEODRIVER python3 test/run_tests.py --build-dir "$build_dir" --filter 'Introduction/*' --filter 'ScriptGenerator/*' --filter 'TurnEngineHarness/the committed match record verifies*' --filter 'MapPreview/generator Studio*' --filter 'OrderValidation/*' --filter 'OrderAlterateArea/*' --filter 'MapPaintedAreas/*' --filter 'TerrainProperties/*' --filter 'TerrainRuntime/*' --no-display --artifacts artifacts/training/review-native --junit artifacts/training/review-native.xml > artifacts/training/review-native.log 2>&1
DISPLAY= SDL_VIDEO_DRIVER=x11 python3 test/run_tests.py --build-dir "$build_dir" --filter 'IntroductionUI/*' --tag display --artifacts artifacts/training/review-ui --junit artifacts/training/review-ui.xml > artifacts/training/review-ui.log 2>&1
