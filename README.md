# PR 1187 focused native verification

Tested source: `6808e584c721dc2ca78afc7d6dd6f0eca72b0b36`.
PR base: `dd493d7e71d21e5ab6cbaee0fe168a37e076860a`.
Fetched master before final validation: `ec1a0adfb6c0f84e3ab60058cf7513441d62ab23` (independent BitArray-only change). `git merge-tree --write-tree HEAD origin/master` reports no conflicts. The combined tree was not executed; this evidence identifies the PR branch explicitly.

## Results

- 74 selected engine cases ultimately passed. The first four-job run passed 72 and hit the 120-second timeout in two cases, with no assertion failures. Retrying only those cases serially with `--timeout 600` passed: no-AI worker/diagnostic/save continuation in 146.4 seconds, multi-engine AI/latency/loss/verifier coverage in 81.6 seconds.
- 22 selected native unit cases passed, including 7 replay-step/version cases, the hash/RNG suites, simulation-version response handling, turn admission and match-record validation.
- Standalone WordChecksum + EntityRandom suites passed on ARM64 with undefined-behavior sanitization and as x86-64 executables on the ARM64 host: 9 cases / 3,393 assertions per architecture.
- The simulation-version checker's 9 self-tests passed. `test/check_sim_revision.py --base origin/master`, `test/check_entity_random.py`, and `git diff --check` passed.
- Regenerated the golden match record and trace, then verified the committed fixture without update mode in the final engine run. The trace covers ticks 0 through 841.

`final-native.xml` combines the 72 initial engine passes, 2 successful retries, and 22 unit passes (96 cases). `engine.xml` retains the original two timeout errors; `engine-retry.xml` records the successful reruns. The combined file is a resolved-coverage summary, not a claim that the first pass was green. All source-level checks were left enabled.

## Environment and scope

macOS 26.6.2 ARM64, Apple Clang 21.0.0, C++20. Production engine objects use the normal full-featured fast-development flags `-O0 -g1`, compiler caching, no PCH/unity. `environment.json` records dependency versions; `compiled-provenance.json` records the clean tested revision, flags, and source-tree fingerprint embedded in the test executables.

A temporary SCons wrapper restricts test registration to six engine test files and six unit test files, plus the existing mobile document/certificate harnesses needed for their shared unit-test doubles. Production source lists, feature configuration and compiler/linker flags are unchanged. The tracked test registry and build scripts were not edited. The full test suite was deliberately not run.

The evidence archive retains the initial Apple Clang frontend mutex crashes, the interrupted broad build, and temporary focused-harness setup failures (Python import path during asset export and omitted unit test-double providers). The wrapper was corrected, production objects were reused, and the final clean-revision focused build linked both binaries successfully. These setup failures did not require a production-code change.

## Reproduce

Check out the tested source revision, then copy the files from this evidence tree's `harness/` directory into `artifacts/checksum-draft/` in that checkout. Run from the repository root:

```sh
PYTHONPATH="$PWD" python3 tools/dev_build.py -j8 -f artifacts/checksum-draft/SConstruct.focused tests
PYTHONUNBUFFERED=1 python3 artifacts/checksum-draft/run-focused.py engine
PYTHONUNBUFFERED=1 python3 artifacts/checksum-draft/run-focused.py unit
```

If the two long cases hit the default timeout on a fast-development build, retain that result and run the exact focused retry:

```sh
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --no-display --exclude-tag benchmark --jobs 1 --timeout 600 --filter 'SharedWorkerLifecycle/no-AI completed ticks*' --filter 'TurnEngineHarness/two engines with AI seats*' --artifacts artifacts/checksum-draft/engine-retry --junit artifacts/checksum-draft/engine-retry.xml --write-inventory artifacts/checksum-draft/engine-retry-inventory.json
```

The initial golden regeneration used `python3 artifacts/checksum-draft/run-focused.py golden`; this writes fixtures and is not needed to check the final committed fixture.

Standalone scalar tests:

```sh
clang++ -std=c++20 -O2 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all -Itest/third_party -Itest/support -Isrc/common -Isrc/game -Ilibgag/include artifacts/checksum-draft/TestMain.cpp src/common/WordChecksumTest.cpp src/common/EntityRandomTest.cpp -o artifacts/checksum-draft/checksum-unit-tests
artifacts/checksum-draft/checksum-unit-tests
clang++ -arch x86_64 -std=c++20 -O2 -Wall -Wextra -Itest/third_party -Itest/support -Isrc/common -Isrc/game -Ilibgag/include artifacts/checksum-draft/TestMain.cpp src/common/WordChecksumTest.cpp src/common/EntityRandomTest.cpp -o artifacts/checksum-draft/checksum-unit-tests-x86_64
artifacts/checksum-draft/checksum-unit-tests-x86_64
python3 test/test_check_sim_revision.py
python3 test/check_sim_revision.py --base origin/master
python3 test/check_entity_random.py
git diff --check
```

## Evidence and limits

The archive contains complete build/run logs and generated selected-test saves, replays, match records and checksum traces. The `engine-retry` artifacts contain all twelve completed diagnostic/worker traces from the successful continuation retry; the original run's partial artifacts are retained separately.

Not covered: full-suite execution, Linux/Windows/browser engine execution, full cross-platform per-tick checksums, live mixed-version LAN peer integration, or broader collision/fault studies. The x86-64 scalar run does not establish full x86-64 engine determinism. No optimized end-to-end engine performance measurement was made; development-build timings are test evidence only.
