#!/bin/bash
set -euo pipefail
export GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix
export LD_LIBRARY_PATH="$GLOB2_SDL3_PREFIX/lib"
export CCACHE=1
scons -j16 release=1 server=0 engine-tests unit-tests build/linux/client/release/src/glob2
python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*' --junit artifacts/separation/257-golden-update.xml --artifacts artifacts/separation/257-golden-artifacts
python3 test/run_tests.py --no-display --jobs 8 --filter 'MarketFetch/*' --filter 'HiringBucket/*' --filter 'InnSwap/*' --filter 'LevelGate/*' --filter 'ResourceFetchTarget/*' --filter 'RoundTripHungerGate/*' --filter '*Gradient*/*' --filter 'SavegameSafety/*' --filter 'TeamStatsSave/*' --filter 'ReplayStepCounter/*' --filter 'MatchSetup/*' --filter 'TurnEngineHarness/the committed*' --junit artifacts/separation/257-tests.xml --artifacts artifacts/separation/257-test-artifacts
python3 test/check_sim_revision.py --base origin/master

# #258 uses the same focused filters after cherry-picking its two upgrade commits,
# with report/output names changed from 257 to 258. The final-revision builds
# repeat the same build command after committing regenerated golden fixtures.
# CLI check for each branch:
build/linux/client/release/src/glob2 --verify-match "$PWD/test/fixtures/multiplayer/FourSquares1.g2mr" --map "$PWD/maps/FourSquares1.map.gz" --out "$PWD/artifacts/separation/257-final-verify" --profile pr257-separation-final
cmp artifacts/separation/257-final-verify/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt

# Equivalent CLI command at #258 final head:
# build/linux/client/release/src/glob2 --verify-match "$PWD/test/fixtures/multiplayer/FourSquares1.g2mr" --map "$PWD/maps/FourSquares1.map.gz" --out "$PWD/artifacts/separation/258-final-verify" --profile pr258-separation-final
# cmp artifacts/separation/258-final-verify/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
