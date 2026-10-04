Local verification

- Tested commit SHA: f46b16e7d7bfbd23539cfeee3d5a250a0f58a4d1
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Commands: `GLOB2_SDL3_PREFIX=<same pinned SDK as #257> CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --filter MarketFetch/* --filter SavegameSafety/* --filter SceneExtract/* --filter 'TurnEngineHarness/the committed*' --no-display; python3 test/check_sim_revision.py --base origin/master`
- Result: Native tests build passed; 11 market/save/scene/golden cases passed; all three market levels survive binary and text saves; sim-version contract passed.
- Limitations: macOS arm64 only. No cross-platform per-tick comparison, human visual/balance review or distinct upgrade sprites; costs remain provisional. Draft stack retains #254 balance concern.
