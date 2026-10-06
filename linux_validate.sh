set -e
cd /home/bradley/glob2-read-phase
export GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/sdl3/prefix
export GLOB2_RECORDING_PREFIX=/home/bradley/glob2-building-gradient-20261004-9520/recording-prefix
export LD_LIBRARY_PATH=$GLOB2_SDL3_PREFIX/lib:$GLOB2_RECORDING_PREFIX/lib
python3 -c 'import json,hashlib,pathlib; m=json.load(open("source-hashes.json")); bad=[p for p,h in m.items() if hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()!=h]; assert not bad,bad; print("Exact production/test source hashes match 325357ec2")' > source-verification.log
git add src test docs
git -c user.name=Codex -c user.email=codex@local commit -qm 'Final matching production/test snapshot 325357ec2'
scons -j8 release=1 server=0 tests > verified-build.log 2>&1
python3 test/run_tests.py --no-display --filter '*Gradient*/*' --filter 'ComputeExecutor/*' --filter 'ReadOnlyPhase/*' --filter 'SimulationReadPhase/*' --filter 'SharedWorkerLifecycle/*' --filter 'RuntimeContinuation/*' --filter '*Save*/*' --filter '*Replay*/*' --filter '*Script*/*' --filter 'TerrainProperties/*' --filter '*Market*/*' --filter '*Guard*/*' --filter 'TurnEngineHarness/*' --filter 'TurnHarness/*' --filter '*Version*/*' --filter '*LAN*/*' --filter 'LegacyAIState/*' --filter 'AIStateContinuation/*' --artifacts artifacts/read-phase/verified-tests --junit artifacts/read-phase/verified.xml > artifacts/read-phase/verified.log 2>&1
