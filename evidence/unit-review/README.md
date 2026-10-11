# Configurable units: team review and final verification

Tested feature: `c48c2871ea238df16401bc34bca394293f4f952f`. Rebased cleanly onto master `c1401a484ed788f06238626424de422df9f5dbb0` after its renderer/build integration landed. All 50 feature commits applied without conflicts. Prior validation at `1506403c7` retains its original identity in this archive and the previous evidence commit.

The team reviewed architecture, capability coverage, hybrid assignments/cargo, persistence, protocol boundaries, documentation, comments and runtime costs. Findings were corrected and independently checked. Latest integration review covers renderer factories and shared build inputs; configurable-unit presentation, simulation and the explicit GPU initialization shutdown barrier remain unchanged by the final rebase. Review receipts are included.

Environment: Ubuntu 26.04/Linux x86_64, GCC 15.2.0. Ordinary release with `release=1 dev_fast=0 linker=default`, `-O3`, no fast/PCH/unity. Pinned dependencies are unchanged. Runtime SDL was rebuilt with existing required repository patches; all 75 installed headers match the compile prefix byte-for-byte. See `patched-sdl/manifest.json`. Final source, tree, binaries, environment, commands, timestamps, exit codes and log hashes are in `final-summary.json` and the raw command receipts.

Final results:

- Engine: 291 passed, 8 declared display skips.
- Unit: 993 passed, 62 declared display skips. New renderer display cases account for the larger inventory.
- Presentation: 3 passed under Xvfb/Mesa.
- Programmable GL: 23 display cases passed under Xvfb/Mesa, including skin rig/capture, recreation, authored mips and terrain composition. The compiled default points to generated shaders; fixture/asset provenance is in `gl-shader-evidence/`. SDL GPU renderer display execution is omitted.
- Production CLI: 15 passed; generated guide/man/completions check passed.
- Build/tooling: 506 selected, 502 passed, 3 skipped, 1 inherited failure. `test_shared_code_does_not_embed_browser_api_calls` rejects the Emscripten include in the renderer added to master. The same failure was executed and reproduced on an unmodified archive of `c1401a484`; see `base-platform-boundary.json` and its log. No test weakening or suppression was made.
- Simulation version guard and Python contracts passed.
- Four complete canonical files (replay, match, resource compositions, unit ablations) match frozen producer `9e6f406b634023702eb34125916f74fe453a5e54` byte-for-byte without normalization; hashes/lengths are in `trace-comparison-final.json`.
- All 24 shutdown repetitions exited normally and matched all 842 committed checksums: 4 CPU/two-thread, 16 GPU/two-thread and 4 GPU/automatic. `shutdown-shutdownLatest/` holds the final runs. Earlier GPU abort/hang logs and GDB stacks are preserved in `shutdown-loops/`.
- Fresh GPU-on/off 512-tick games have identical complete checksum traces. The NVIDIA GeForce RTX 2070 SUPER initialized, but these games dispatched no device fields. This comparison is not device-execution parity evidence. Raw counters and availability-dependent test behavior remain explicit.

The shipped roster remains worker, explorer and warrior. No new customization UI/artwork/units or intentional balance changes ship. SIM47, file format 153 and network protocol 69 are current; save support remains format 58 and accepted built-in replay support remains 152. Historical migration preserves gameplay, orders and RNG while retaining previously documented diagnostic differences; complete raw legacy-resume equality is not claimed.

Final execution is Linux/GCC only. Earlier GCC/Clang/macOS/browser evidence is not relabeled as final-head coverage. Final Windows/macOS/browser/mobile/iOS execution, native SDL GPU renderer execution and maintainer play-through remain unverified. Hosted success is not claimed; prior environment failures are retained. Performance requirements were explicitly removed by the user, so no final performance claim is made. Known unrelated master failures impose no merge restriction under repository instructions; this tooling failure is disclosed above.

Exact final argv (environment and results are in the corresponding receipt):

- `python3 tools/dev_build.py release=1 dev_fast=0 linker=default --build=build/unit-review-release build/unit-review-release/src/glob2 engine-tests unit-tests -j6` — PASS (exit 0).
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/engineLatest.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/engineLatest-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/engineLatest-cases --binary engine --no-display --filter UnitCatalog/* --filter UnitCustomization/* --filter TeamStatsSave/* --filter EngineSession/* --filter LegacyReplay/* --filter GameDiagnostics/* --filter CommandLine/* --filter BuildingCatalog/* --filter BuildingProductionCombat/* --filter WorldSnapshot/* --filter TerrainHazardRouting/* --filter TerrainRuntime/* --filter Gradient/* --filter GradientPreparation/* --filter GradientPipeline/* --filter ComputeExecutor/* --filter BuildingGradientInvalidation/* --filter MapGradientInvalidation/* --filter TurnEngineHarness/the committed*` — PASS (exit 0).
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/unitLatest.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/unitLatest-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/unitLatest-cases --binary unit --no-display` — PASS (exit 0).
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/presentationLatest.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/presentationLatest-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/presentationLatest-cases --binary engine --filter UnitCatalog/new combinations retain appearance* --filter ColonySkinPreview/*` — PASS (exit 0).
- `python3 test/test_cli_smoke.py --binary build/unit-review-release/src/glob2 --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/cliLatest-cases --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/cliLatest.xml` — PASS (exit 0).
- `python3 tools/cli_reference.py --binary build/unit-review-release/src/glob2 --check` — PASS (exit 0).
- `python3 test/run-browser-determinism.py build/unit-review-release/src/glob2 /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/tracesLatest --engine-binary build/unit-review-release/test/glob2-engine-tests` — PASS (exit 0).
- `python3 artifacts/unit-review/gpu_cli_check.py gpuLatest` — PASS (exit 0).
- `python3 artifacts/unit-review/shutdown_fixed_check.py shutdownLatest` — PASS (exit 0).
- `python3 test/check_sim_revision.py --base 32c7ec892b5a1dfb22bde2d9025de106e8eac4d3` — PASS (exit 0).
- `python3 -m unittest discover -s test -p test_check_sim_revision.py` — PASS (exit 0).
- `/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -p test_*.py` — FAIL (exit 1).
- `python3 test/run_tests.py --build-dir build/unit-review-release -j 6 --junit /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/glLatest.xml --write-inventory /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/glLatest-inventory.json --artifacts /home/bradley/.codex/worktrees/af25/glob2/artifacts/unit-review/glLatest-cases --binary unit --filter ProgrammableGLDevice/*` — PASS (exit 0).

The archive includes source-bound reviews, raw logs/JUnit/inventories, saves/replays/checksums, original failure diagnostics and dependency provenance. Dependency staging, temporary base checkout, builds and publication intermediates are excluded. Evidence and helper scripts are deliberately outside the feature branch.
