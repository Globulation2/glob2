# Reproduction commands

macOS release build (Apple clang 21, SDL3 prefix cached locally):
```sh
CCACHE=1 GLOB2_SDL3_PREFIX=/Users/bradley/.cache/glob2-sdl3/prefix scons -j6 release=1 build/darwin/client/release/src/glob2 build/darwin/client/release/test/glob2-engine-tests build/darwin/client/release/src/MapGeneratorGoldenTest build/darwin/client/release/test/glob2-unit-tests
python3 test/test_tournaments.py -v
python3 test/test_tournament_pipeline.py
python3 test/test_map_fairness_tournament.py
python3 test/test_map_generation_study.py
python3 test/test_map_cli.py build/darwin/client/release/src/glob2
python3 test/run_tests.py --filter 'MapGeneratorDefaults/*' --filter 'CustomGameSetup/preferences;*' --filter 'CustomGameSetup/custom game screens;*' --filter 'MatchSetup/*' --filter 'ReplayStepCounter/*' --filter 'TurnEngineHarness/the committed*'
python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'
python3 test/run_tests.py --filter 'TurnEngineHarness/the committed*'
python3 test/run_tests.py --binary unit --filter 'ReplayStepCounter/*'
python3 test/check_sim_revision.py --base origin/master
build/darwin/client/release/src/MapGeneratorGoldenTest artifacts/parameter-search/golden-integrated-profile --require-rows
```

Linux uses GCC 15.2 and the same source, with GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix, -j8 and build/linux/client/release. The native runner excludes the two display/UI selections on Linux. Both platforms run --verify-match with absolute paths to test/fixtures/multiplayer/FourSquares1.g2mr and maps/FourSquares1.map.gz, then compare checksums.txt byte-for-byte with each other and the committed trace.

Platform checks (Node 24.14, npm ci --ignore-scripts; PostgreSQL 16 listening only on loopback port 55432 with a disposable test cluster):
```sh
cd platform
npm run typecheck
npm exec -- vitest run packages/core/test/queueConfig.test.ts packages/play/test/warmMaps.test.ts apps/worker/test/matchmaker.test.ts
npm exec -- vitest run apps/engine-agent/test/engineCli.test.ts packages/protocol/test/fixtures.test.ts
npm exec -- prettier --check packages/core/src/queueConfig.ts packages/core/test/queueConfig.test.ts
npm exec -- eslint packages/core/src/queueConfig.ts packages/core/test/queueConfig.test.ts
```

Individual AI/map requests and exact command arrays are inside game-and-preview-evidence.tar.gz; earlier frozen study binaries/source identities, requests and failure replays are in study-evidence.tar.gz. These studies are partial and precede the final master integration.
