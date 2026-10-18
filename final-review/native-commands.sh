#!/bin/sh
set -eu
b=build/darwin/client/debug/dev-dev_fast-true-linker-auto
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'HiringPipeline/*' --filter 'HiringGradientPreparation/*' --filter 'HiringPlanner/*' --filter 'InitialBuildingGradient/*' --filter 'UnitContinuation/*' --filter 'BuildingGradient*/*' --filter 'GradientPipeline/*' --filter 'SavegameSafety/*' --filter 'BuildingProductionCombat/*' --filter 'BuildingServices/*' --filter 'BuildingMaterialDemand/*' --no-display -j4 --junit artifacts/final-pr-review/engine.xml --artifacts artifacts/final-pr-review/engine
python3 test/run_tests.py --build-dir "$b" --binary unit --filter 'Replay*/*' --filter 'PlatformProtocol/*' --filter 'PlatformClient/*' --no-display -j4 --junit artifacts/final-pr-review/compatibility.xml --artifacts artifacts/final-pr-review/compatibility
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'TurnEngineHarness/*' --no-display -j4 --junit artifacts/final-pr-review/turn.xml --artifacts artifacts/final-pr-review/turn
python3 test/check_sim_revision.py --base origin/master
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'UnitCustomization/*' --filter 'TrappedUnitLifecycle/*' --filter 'EntityRandomLifecycle/*' --filter 'EnteringUnitSave/*' --filter 'LegacyRoundTripSave/*' --filter 'HungryDefeat/*' --filter 'ResourceFetchTarget/*' --filter 'FetchHiringScore/*' --no-display -j4 --junit artifacts/final-pr-review/movement.xml --artifacts artifacts/final-pr-review/movement
