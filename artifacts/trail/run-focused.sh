#!/usr/bin/env bash
set -euo pipefail
root=/home/bradley/.codex/worktrees/6eeb/glob2
cd "$root"
env -u DISPLAY LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 python3 test/run_tests.py \
 --filter 'TerrainProperties/*' --filter 'TerrainPresentation/*' \
 --filter 'TerrainEcology/*' --filter 'MapQuery/*' --filter 'FertilityField/*' \
 --filter 'EditorActionCoverage/*' --filter 'SettingsExperiments/*' \
 --filter 'ExperimentalFeatures/*' --filter 'JavaScriptIntegration/*' \
 --filter 'MatchSetup/*' --jobs 8 --display-jobs 1 \
 --artifacts artifacts/trail/tests --junit artifacts/trail/tests/junit.xml
python3 test/test_map_report.py build/linux/client/release/src/glob2 build/linux/client/release/test/MapReportHarness
