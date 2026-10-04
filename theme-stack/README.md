# Current-master compatibility and build repairs

Final tested repair SHA `47a85461571fd9bc4e5944765493d0b11b9ef023`, integrated base `0eecf07d33876deafdd78212596b3db4216c08f5` (includes release metadata and source-tree restructuring). Local Ubuntu 26.04 x86_64: GCC 15.2, GCC 11.5 syntax check, SDL3 3.4.16 and pinned companion libraries, native release `-std=gnu++20 -Wall -fPIC -O3 -s`. Docker engine-agent uses Ubuntu 24.04/GCC 13 and its own pinned SDL/recording dependency builds. Exact flags and clean binary provenance are retained.

Five reproduced failures are repaired:

- GCC 11 cannot parse the JSON template call in ThemeCatalog::readColors without the template disambiguator. `theme-gcc11-before.log` reproduces the hosted error; `theme-gcc11-after.log` and `theme-gcc11-integrated.log` pass. Their JSON files contain exact compiler argv arrays.
- The Docker C++ build stage omitted NASM, required by embedded x264. `new-master-platform-stack.log` contains the actual hosted failure. Add NASM only to the build stage.
- The translation audit's reviewed shared vocabulary omitted five legitimate theme labels. Dictionary review confirms French [Dune](https://www.dictionnaire-academie.fr/article/A9D3350), and Ocean in [Danish](https://ordnet.dk/ddo/ordbog/ocean), [Polish](https://wsjp.pl/haslo/podglad/1640/ocean/515156/woda), [Romanian DEX](https://dexonline.ro/definitie/ocean) and [Slovenian SSKJ](https://fran.si/iskanje?Query=ocean&View=2). Add only these language-specific vocabulary values, preserving catalog translations and the fallback check. `theme-translations-before.log` reproduces all five false positives; all five translation tests pass afterward, and strict audit has zero structural errors. Registered pending strings introduced by recording remain outside this repair.
- Cortex's observation layout intentionally advanced to v21 with effective rule capabilities. Its version fixture still expected v20. Keep the strict exact layout assertion and update it to v21; action layout stays v13.
- Disabled upgrades intentionally stop restored trainees without incrementing training visits. The stats fixture still expected one visit. Update that exact counter to zero and strengthen the assertion to require DIS_EXITING_BUILDING, unchanged ability level and zero ability gain. The save/load and ring-wrap scenarios remain intact. `rules-fixtures-before.log` independently reproduces both stale rules expectations.

The repair does not change simulation rules, RNG, serialization, replay/network gates, theme parsing or gameplay feel. Existing simulation revision 12 and committed golden record remain unchanged.

## Final validation

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons -j12 release=1 server=0 tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib LP_NUM_THREADS=2 GLOB2_TEST_ARTIFACTS_ROOT=artifacts/ci-repair/theme-stack-final python3 test/run_tests.py --filter 'ThemeCatalog/*' --filter 'RecordingSession/*' --filter 'GameplayRecording*' --filter 'WindowResize/*' --filter 'Settings/*' --filter 'TextMetrics/*' --filter 'AITelemetryUI/*' --filter 'AIRules/*' --filter 'TeamStatsSave/*' --filter 'CortexUpgrade/*' --filter 'MapGeneratorDefaults/Explicit designs preserve pre-Random golden worlds' --display-jobs 1 --jobs 8 --fullscreen --artifacts artifacts/ci-repair/theme-stack-final --junit artifacts/ci-repair/theme-stack-final.xml
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib LP_NUM_THREADS=2 GLOB2_TEST_ARTIFACTS_ROOT=artifacts/ci-repair/full-unit-final python3 test/run_tests.py --binary unit --display-jobs 1 --jobs 4 --fullscreen --artifacts artifacts/ci-repair/full-unit-final --junit artifacts/ci-repair/full-unit-final.xml
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib LP_NUM_THREADS=2 GLOB2_TEST_ARTIFACTS_ROOT=artifacts/ci-repair/engine-quick-final python3 test/run_tests.py --binary engine --quick --no-display --jobs 8 --artifacts artifacts/ci-repair/engine-quick-final --junit artifacts/ci-repair/engine-quick-final.xml
python3 -m unittest discover -s test -p test_translations.py -v
python3 data/check_translations.py --strict
docker buildx build --load --progress=plain --target engine-agent --build-arg JOBS=12 --label org.glob2.verification-revision=47a854615 -t glob2-ci-repair-engine:47a85461 -f deploy/Dockerfile .
mkdir -p artifacts/ci-repair/engine-repaired-match
chmod 777 artifacts/ci-repair/engine-repaired-match
docker run --rm --entrypoint /opt/glob2/bin/glob2 --workdir /opt/glob2/share --env SDL_VIDEODRIVER=dummy --env SDL_AUDIODRIVER=dummy --env HOME=/tmp --volume /home/bradley/.codex/worktrees/c59e/glob2/test:/tests:ro --volume /home/bradley/.codex/worktrees/c59e/glob2/artifacts/ci-repair/engine-repaired-match:/output glob2-ci-repair-engine:47a85461 --verify-match /tests/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out /output
cmp artifacts/ci-repair/engine-repaired-match/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
```

All exit zero. Focused integration: 53 process groups, 69 cases, no skips/failures, including software/OpenGL fullscreen and recording per-tick comparisons. Entire unit suite: 14 groups, 706 cases, no skips/failures. Fast non-display engine suite: 514 passed, zero failed; 77 intentionally omitted display/slow cases. Docker image builds, loads and verifies the golden match as runtime user 10001: 702 per-tick checksums, exact byte-for-byte match to the committed trace. Retained match replay, verdict/results and recording saves/replays/checksum/video artifacts support these claims.

This local verification is Linux only; no macOS/Windows/cross-platform equivalence or human playtest is claimed. Full hosted verification is requested; the post-merge current-master matrix must establish full integration/platform coverage, including later tournament tooling changes. No selected CI cases, checks or platform gates were removed. Older preliminary logs/provenance remain labeled with their own tested revisions; final-* and rules-final-build files describe the final repair revision.
