Local verification

- Tested final commit SHA: 258c853a79e54731f172003ee9f27ef35049f56d
- Final base: 8bc1b897ff7b30b094d35dd86c3230828988c457 (rebased PR head).
- Environment: macOS, arm64; Python 3.14.7; Apple Clang 21.0.0.
- Native SDK where applicable: SDL3 3.4.16, SDL3_ttf 3.2.2, SDL3_image 3.4.6, SDL3_net 3.2.0; release=1, -j6, CCACHE=1.
- Final verification: Native binaries rebuilt on final rebased head; focused checks passed. Exact command filters and results in latest-master integration log; prior broader evidence remains linked at previous_validation_sha.
- Commands/results: `GLOB2_SDL3_PREFIX=<pinned SDK> CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --junit artifacts/pr-refresh/258-latest-master-junit.xml --filter 'MarketFetch/*' --filter 'SceneExtract/*' --filter 'SavegameSafety/*' --filter 'TurnEngineHarness/the committed*' --no-display; python3 test/check_sim_revision.py --base origin/master; git diff --check origin/master...HEAD`; final rebase and focused commands/results also recorded in 258-latest-master-integration.log where present.
- Limitations: macOS arm64 only. No cross-platform per-tick comparison, human visual/balance review or distinct upgrade sprites; costs remain provisional. Draft stack retains #254 balance concern.

Earlier broader validation

- Commit: f46b16e7d7bfbd23539cfeee3d5a250a0f58a4d1
- Base: a05d6cd8cbe594a4d281bb6e753edbe1b8d10899
- Commands: `GLOB2_SDL3_PREFIX=<same pinned SDK as #257> CCACHE=1 scons -j6 release=1 tests; python3 test/run_tests.py --filter MarketFetch/* --filter SavegameSafety/* --filter SceneExtract/* --filter 'TurnEngineHarness/the committed*' --no-display; python3 test/check_sim_revision.py --base origin/master`
- Results: Native tests build passed; 11 market/save/scene/golden cases passed; all three market levels survive binary and text saves; sim-version contract passed.

