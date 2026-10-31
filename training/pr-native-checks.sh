#!/usr/bin/env bash
set -euo pipefail
root_dir=/home/bradley/.codex/worktrees/159d/glob2
cd "$root_dir"
build_dir=build/linux/client/debug/dev-dev_fast-true-linker-auto
binary="$build_dir/src/glob2"
python3 tools/cli_reference.py --binary "$binary"
python3 tools/cli_reference.py --binary "$binary" --check
python3 tools/tutorials/build.py --check --maps --binary "$binary" > artifacts/training/pr-regeneration.log 2>&1
env -u SDL_VIDEO_DRIVER -u SDL_VIDEODRIVER python3 test/run_tests.py --build-dir "$build_dir" --filter 'Introduction/*' --filter 'ScriptGenerator/*' --filter 'TurnEngineHarness/the committed match record verifies*' --filter 'MapPreview/generator Studio*' --no-display --artifacts artifacts/training/pr-native --junit artifacts/training/pr-native.xml > artifacts/training/pr-native.log 2>&1
DISPLAY= SDL_VIDEO_DRIVER=x11 python3 test/run_tests.py --build-dir "$build_dir" --filter 'IntroductionUI/*' --tag display --artifacts artifacts/training/pr-ui --junit artifacts/training/pr-ui.xml > artifacts/training/pr-ui.log 2>&1
