Snapshot entity reuse verification — PR #1173

Tested commit: 3e9ed25c818975129a433aa1de8df38213b77c6c
Base/master: 6b95c81c0d438f64e3391ff4785bc95d3436d174 (fetched before final revision validation; no newer master commits)
Environment: macOS 26.6.2, Darwin arm64, Apple Clang 21.0.0.
Existing managed native dependency prefix build-dd8d40b0b2e044b88374d32e and Homebrew libraries; no dependencies added. Compiler/link commands, resolved libraries, vcpkg inputs, binary hashes and compiled test provenance are attached. Development flags -O0 -g1, dev_fast=1; no PCH/unity. This is correctness/mechanism evidence, not release performance qualification.

Final build command:
PYTHONPATH="$PWD" python3 tools/dev_build.py engine-tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2

Final test command:
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'WorldSnapshot/*' --filter 'SceneExtract/*' --filter 'AIPipeline/*' --filter 'GradientPreparation/*' --filter 'SharedWorkerLifecycle/*' --filter 'SimulationReadPhase/*' --filter 'TurnEngineHarness/the committed*' --no-display --timeout 300 -j 2 --junit artifacts/snapshot-reuse/final-junit.xml --artifacts artifacts/snapshot-reuse/final-tests

Result: 102 passed, one existing OpenCL completion-count failure, one display skip. New pointer/retained-record/copy-accounting and all five invalidation controls pass. AI coverage includes nine controllers, delays 0..8 and participants 1/2/4/8 with orders/state comparisons and save continuation. Scene and shared-worker lifecycle/read-phase cases pass.

Known failure: GradientPreparation/modified terrain costs remain exact when field obstacles change expects an OpenCL commit but reports zero; computed field checks remain exact. A matched control forces unchangedBoundary=false, disabling entity reuse, and reproduces the same failure. This is a source-qualified disabled-reuse control, not a separate full-master build. The source patch, binary hash, provenance, JUnit and raw logs are retained. The control was restored before final candidate compilation. Existing master failures do not restrict merge under AGENTS.md; no GPU repair is included.

Verifier commands (N=1 and 4), fresh isolated GLOB2_USER_DATA_DIR and SDL dummy drivers:
build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2 match verify test/fixtures/multiplayer/FourSquares1.g2mr --map-file maps/FourSquares1.map.gz --output-dir artifacts/snapshot-reuse/verify-final-N --compute-threads N
Both verdicts verified, 842 checksum records each. Both checksums.txt files equal the committed trace byte-for-byte; SHA-256 2287760e6a3f627e08584369f4cb7c202019d69458be4751d42790ebd19b3601. Actual produced traces/replays/verdicts are attached.

python3 test/check_sim_revision.py --base 6b95c81c0d438f64e3391ff4785bc95d3436d174: passed. No simulation arithmetic, saved-state layout, replay/network gate or publication deadline change. No version bump needed. git diff --check: passed.

Retained initial failures: asset worker import needed PYTHONPATH; a new test's subcase JUnit shape was replaced with a loop; the first CLI invocation supplied an unsupported backend option and was corrected. These are not presented as passing runs.

Producer dirty flags: final tracked source matches the tested commit; a pre-existing untracked .playwright-mcp/ directory is recorded in the producer source identity and left untouched. Baseline control additionally records its deliberate tracked edit.

Omitted: Windows/Linux/browser/mobile runtime equivalence, physical GPU/display/manual play, release benchmarks, sanitizers and packaging. The change is portable shared-pointer reuse with conservative existing invalidation guards; no whole-game timing improvement is claimed. Acceptance: focused local evidence accepted by the author under the user's explicit request to extract and merge.
