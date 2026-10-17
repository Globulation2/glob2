#!/bin/sh
set -eu
b=build/darwin/client/debug/dev-dev_fast-true-linker-auto
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'HiringPipeline/*' --filter 'HiringGradientPreparation/*' --filter 'HiringPlanner/*' --filter 'BuildingGradientInitialization/*' --filter 'UnitContinuation/*' --filter 'BuildingGradient*/*' --filter 'GradientPipeline/*' --filter 'SavegameSafety/*' --filter 'BuildingProductionCombat/*' --filter 'BuildingServices/*' --filter 'BuildingMaterialDemand/*' --no-display -j4 --junit artifacts/lazy-restore/engine.xml --artifacts artifacts/lazy-restore/engine
python3 test/run_tests.py --build-dir "$b" --binary unit --filter 'Replay*/*' --filter 'PlatformProtocol/*' --filter 'PlatformClient/*' --no-display -j4 --junit artifacts/lazy-restore/compatibility.xml --artifacts artifacts/lazy-restore/compatibility
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'TurnEngineHarness/the committed*' --update-fixtures --no-display -j1 --junit artifacts/lazy-restore/golden.xml --artifacts artifacts/lazy-restore/golden
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'TurnEngineHarness/*' --no-display -j4 --junit artifacts/lazy-restore/turn.xml --artifacts artifacts/lazy-restore/turn
python3 test/check_sim_revision.py --base origin/master
