#!/bin/bash
set -eu
out=artifacts/engine-performance/pr-final
build=build/linux/client/debug/dev-dev_fast-true-linker-auto
env -u DISPLAY -u WAYLAND_DISPLAY -u SDL_VIDEO_DRIVER -u SDL_VIDEODRIVER python3 test/run_tests.py --build-dir "$build" --filter 'ComputeExecutor/*' --filter 'PerformanceTelemetry/*' --filter 'ReadOnlyPhase/*' --filter 'EngineSession/*' --filter 'TeamStatsSave/*' --filter 'RenderDevice/*' --filter 'ProgrammableGLDevice/*' --filter 'ReplayStepCounter/*' --filter 'JavaScriptCompatibility/*' --filter 'MatchSetup/*' --filter 'SimVersion/*' --filter 'SavegameSafety/*' --no-display --timeout 180 -j6 --junit "$out/native-headless.xml" --artifacts "$out/native-headless" > "$out/native-headless.log" 2>&1 &
headless=$!
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEO_DRIVER=x11 python3 test/run_tests.py --build-dir "$build" --filter 'EngineSession/*' --filter 'TeamStatsSave/*' --filter 'GUIInteractionCoverage/statistics topics*' --filter 'RenderDevice/*' --filter 'ProgrammableGLDevice/*' --tag display --timeout 180 -j4 --junit "$out/native-display.xml" --artifacts "$out/native-display" > "$out/native-display.log" 2>&1 &
display=$!
status=0
wait "$headless" || status=1
wait "$display" || status=1
exit "$status"
