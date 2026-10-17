# Run from the repository checkout of the tested revision.
# Build before preflight and after committing the regenerated fixtures:
# PYTHONPATH=. python3 tools/dev_build.py -j4 engine-tests unit-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2
# Pre-commit focused verification:
# python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'UnitContinuation/*' --no-display -j4 --junit artifacts/flyer-default/preflight.xml --artifacts artifacts/flyer-default/preflight
# Regenerate the committed match before committing:
# python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'TurnEngineHarness/the committed*' --update-fixtures --no-display -j1 --junit artifacts/flyer-default/golden.xml --artifacts artifacts/flyer-default/golden
#!/bin/sh
set -eu
b=build/darwin/client/debug/dev-dev_fast-true-linker-auto
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'HiringPipeline/*' --filter 'HiringGradientPreparation/*' --filter 'HiringPlanner/*' --filter 'InitialBuildingGradient/*' --filter 'UnitContinuation/*' --filter 'BuildingGradient*/*' --filter 'GradientPipeline/*' --filter 'SavegameSafety/*' --filter 'BuildingProductionCombat/*' --filter 'BuildingServices/*' --filter 'BuildingMaterialDemand/*' --no-display -j4 --junit artifacts/flyer-default/engine.xml --artifacts artifacts/flyer-default/engine
python3 test/run_tests.py --build-dir "$b" --binary unit --filter 'Replay*/*' --filter 'PlatformProtocol/*' --filter 'PlatformClient/*' --no-display -j4 --junit artifacts/flyer-default/compatibility.xml --artifacts artifacts/flyer-default/compatibility
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'TurnEngineHarness/*' --no-display -j4 --junit artifacts/flyer-default/turn.xml --artifacts artifacts/flyer-default/turn
python3 test/check_sim_revision.py --base origin/master
python3 test/run_tests.py --build-dir "$b" --binary engine --filter 'UnitCustomization/*' --filter 'TrappedUnitLifecycle/*' --filter 'EntityRandomLifecycle/*' --filter 'EnteringUnitSave/*' --filter 'LegacyRoundTripSave/*' --filter 'HungryDefeat/*' --filter 'ResourceFetchTarget/*' --filter 'FetchHiringScore/*' --no-display -j4 --junit artifacts/flyer-default/movement.xml --artifacts artifacts/flyer-default/movement

# Participant comparisons were also run on the committed build:
# python3 docs/.work/pr1197-flyer-matches.py > artifacts/flyer-default/matches.log 2>&1
