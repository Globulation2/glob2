# Configurable units: independent review and final integration evidence

Tested feature commit: `1506403c7a154ba4a200e45312f2ee5793d86568`. Engine base: `0661f9605046958047d06fb6600546c24f139cd5`. Final observed master: `9d0685963b1e0c3c09dc767cad3c68596bde51e9`; its additional changes concern Mac signing/release tooling, with no runtime/build-input overlap and a clean computed merge. See `final-master-integration.json`.

The feature ships the three existing units and configurable developer definitions. Review fixes cover presentation identity and animation bounds, independent idle clearing, cached legacy vision, production examples, loading stages, cargo/interaction ownership comments, and water-only routing minima. Fresh reviewers checked the latest GPU/camera integration. Execution then exposed a GPU initialization/exit race inherited from that integration and stale generated CLI references. A restartable owner shutdown barrier, gated lifecycle regression and regenerated references resolve these findings. SIM47 has an engine-generated match record; its gameplay checksum trace did not change.

Environment: Ubuntu26.04/Linux x86_64, GCC15.2.0. Ordinary release flags `release=1 dev_fast=0 linker=default`, `-O3`; no fast/PCH/unity build. Existing pinned dependency versions were reused. The borrowed compile SDL prefix lacked a required PNG preservation patch: the isolated runtime rebuild applies all repository patches and all75 installed headers match the compile prefix byte-for-byte. See `patched-sdl/manifest.json`, the command receipts and `runtime-dependency-correction.json` for flags, hashes and source boundaries.

Final results:

- Engine:291 passed,8 declared display skips.
- Unit:990 passed,24 declared display skips, including the background initialization/join/restart regression.
- Presentation:3 passed under Xvfb/Mesa.
- Production CLI:15 passed; generated guide/man/completions check passed.
- Build/tooling contracts:494 passed,3 declared skips (497 selected).
- Simulation revision guard and its Python contracts passed.
- Four complete canonical files (replay, match, resource compositions and unit ablations) match frozen producer `9e6f406b634023702eb34125916f74fe453a5e54` byte-for-byte, without normalization. Hashes and lengths are in `trace-comparison-final.json`.
- On the NVIDIA GeForce RTX2070SUPER, fresh GPU-on/off512-tick structured runs have identical complete replay checksums. Actual device capability and execution counters are retained; these512-tick games did not dispatch fields. Direct OpenCL tests retain their availability-dependent behavior.
- Fixed shutdown repetitions:4 CPU/two-thread,16 GPU/two-thread and4 GPU/automatic runs all exit0 and match all842 committed checksums. Prior GPU/two-thread abort/hang logs and GDB stacks are retained in `shutdown-loops/`. The fixed runs are in `shutdown-fixed/`.

Coverage addresses catalogs, high IDs, capability combinations, cargo, recipes, availability, legacy save/replay loading, snapshot continuation, pending gradients, pathfinding, camera sessions, stock rendering identity and CLI integration. Original failed invocations are preserved with corrected results: fixture expectation, dependency environments, intentionally interrupted build, GPU shutdown, reference drift and a development helper refusing a reused output directory. No sanitizer suppressions or weakened runtime assertions were introduced.

Final-head execution is Linux/GCC only. Earlier GCC/Clang/macOS/browser evidence keeps its own source identity and is not relabeled as final-head execution. Final Windows/macOS/browser/mobile/iOS execution and a maintainer play-through remain unverified. Hosted failures/limits from the preceding revision are classified in `docs-ci/review-receipt.json`; hosted success is not claimed here. The user removed performance measurement and acceptance requirements, so no final performance claim is made.

Exact final commands (raw argv, environment, source/tree identity, timestamps, exit status and log hash in each `*-command.json`):

- `python3 tools/dev_build.py release=1 dev_fast=0 linker=default --build=build/unit-review-release build/unit-review-release/src/glob2 engine-tests unit-tests -j6` — exit 0.
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/engine47shutdown.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/engine47shutdown-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/engine47shutdown-cases --binary engine --no-display --filter UnitCatalog/* --filter UnitCustomization/* --filter TeamStatsSave/* --filter EngineSession/* --filter LegacyReplay/* --filter GameDiagnostics/* --filter CommandLine/* --filter BuildingCatalog/* --filter BuildingProductionCombat/* --filter WorldSnapshot/* --filter TerrainHazardRouting/* --filter TerrainRuntime/* --filter Gradient/* --filter GradientPreparation/* --filter GradientPipeline/* --filter ComputeExecutor/* --filter BuildingGradientInvalidation/* --filter MapGradientInvalidation/* --filter TurnEngineHarness/the committed*` — exit 0.
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/unit47shutdown.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/unit47shutdown-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/unit47shutdown-cases --binary unit --no-display` — exit 0.
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/presentation47shutdown.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/presentation47shutdown-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/presentation47shutdown-cases --binary engine --filter UnitCatalog/new combinations retain appearance* --filter ColonySkinPreview/*` — exit 0.
- `python3 test/test_cli_smoke.py --binary build/unit-review-release/src/glob2 --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/cli47shutdown-cases --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/cli47shutdown.xml` — exit 0.
- `python3 tools/cli_reference.py --binary build/unit-review-release/src/glob2 --check` — exit 0.
- `python3 test/run-browser-determinism.py build/unit-review-release/src/glob2 /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/traces47shutdown --engine-binary build/unit-review-release/test/glob2-engine-tests` — exit 0.
- `python3 artifacts/unit-review/gpu_cli_check.py gpu47shutdown2` — exit 0.
- `python3 artifacts/unit-review/shutdown_fixed_check.py` — exit 0.
- `python3 test/check_sim_revision.py --base 32c7ec892b5a1dfb22bde2d9025de106e8eac4d3` — exit 0.
- `python3 -m unittest discover -s test -p test_check_sim_revision.py` — exit 0.
- `/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -p test_*.py` — exit 0.

The archive contains logs, JUnit, selected-case inventories, saves/replays/checksums, failure diagnostics, review receipts and dependency provenance. It excludes staging dependency binaries/source trees and publication intermediates. Read `final-summary.json` first, then the relevant receipt/log. Dedicated evidence is deliberately outside the feature branch.
