# PR #1274 continuation and generator baseline evidence

Tested head `dc187a0e4c5c69b089aed9ee083f1eb6770bef40`, fetched base `697ad9d29ed4c2c7d5c965c71dd4ad54395e8306`.

## Changes and qualifications

- Format-150 test input restores legacy routing flags which format 178 stopped writing. Original hosted missing-field diagnostics and exception are retained in the macOS excerpt; the corrected historical save is attached. All funded-service and continuation component checksum assertions remain unchanged.
- The building-gradient test checks the correct error for all eight rejected requests, including a pending-building fork. It still verifies every worker trace, delay, pending phase and absence of artifacts on rejection.
- Only 92 stale macOS rows from reviewed #1246 are accepted; exact request set equals that PR's Linux update. Two full fresh-process observations agree; 472 current macOS rows and all Linux rows are unchanged. generator-acceptance.json lists every revision/hash change; source PR/diff and original manifest/outputs are attached. The producer's macOS build is clean d80e, and generator implementation files are identical at this head. 73 hashes change, 19 are equal. No current-revision discrepancy is accepted.

## Commands and results

- GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix python3 tools/dev_build.py engine-tests: pass. GCC15.2 development correctness build (-O0 -g1); compiled head/clean source/flags in engine-provenance-qualified.json.
- python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'EntityRandomLifecycle/*' --junit artifacts/ci-watch/1906/entity-rng-qualified.xml --artifacts artifacts/ci-watch/1906/entity-rng-qualified: 14 pass, zero failures/skips. Includes current binary/text RNG continuity, malformed RNG rejection and historical funded services through current saves.
- python3 -m unittest discover -s test/build_system -p 'test_generator_evidence.py': 5 pass.
- python3 -m unittest discover -s test/build_system -p 'test_ci*.py': 163 tests, zero failures, 2 existing skips.
- python3 tools/check_docs.py: 310 documents, zero errors. Python compilation and git diff --check pass.
- gh run download 40540201313 -n linux-test-programs-ubuntu-22.04: retained GCC11 release CLI plus producer provenance. Only executable and pinned library members extracted into isolated artifacts directory. Its game/gradient implementation is unchanged by this PR.
- LD_LIBRARY_PATH=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix/lib python3 test/check_gradient_pipeline.py artifacts/ci-watch/1906/hosted-linux/build/linux/client/release/src/glob2 --output artifacts/ci-watch/1906/pipeline-gcc11: complete final harness passes; full results, commands, checksums, saves, rejected inputs and logs in gradient-gcc11-evidence.tar.gz. Private disposable profiles omitted. This validates the final Python harness against the actual previously failing compiler build; it is local execution, not hosted success.

## Limits

No simulation code, save/replay acceptance gates, runtime dependencies or historical tables change. Existing migration/continuation assertions are preserved. Native macOS and Windows execution of the repaired save fixture remains pending. The author accepts this relevant local and hosted evidence under repository policy and the user's repair merge authorization. Hosted PR checks may be pending at merge. Full master CI must still confirm recovery; active master runs were not cancelled. These are correctness results, not performance/shipping claims.
