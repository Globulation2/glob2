Local verification

- Tested commit SHA: ba7c379faf3fdfe0537dd3453a1f3e155209cd92
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `GLOB2_SDL3_PREFIX=/Users/bradley/glob2/artifacts/recording-sdl3/prefix CCACHE=1 scons -j6 release=1 engine-tests; python3 test/run_tests.py --filter GigRelease/* --filter HiringBucket/* --filter ResourceFetchTarget/* --filter RoundTripHungerGate/* --filter SavegameSafety/* --filter RuntimeContinuation/* --no-display; python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'; python3 test/check_sim_revision.py --base origin/master`
- Result: Engine build passed; 7 focused hiring/save/load cases passed; golden match regenerated and verified; sim-version contract passed.
- Limitations: macOS arm64 only; Linux/Windows/browser per-tick equivalence, human play and fresh balance benchmarks not run. Existing Econo regression review remains unresolved; PR remains draft.
