Latest investigation and narrow production fix: [reconciliation/README.md](reconciliation/README.md). The no-cap experiment exposes an age-support gap and planning liveness failures; successful planning reads no longer trigger periodic demand.

Latest final512 dataset, native verification and completed current-master comparison: [final-512/README.md](final-512/README.md). Final performance is mixed: pooled CPU/tick+16.51%,3/8 format wins. The older evidence below used the pre-integration base and does not establish final performance.

Local / VM verification for adaptive resource gradients

- Production commit: f5636d46430c7c948825f4430ea4e79822c1bf6a.
- Final test-only follow-up: 1aa2d2539 (changes only TerrainPropertiesTest.cpp and SavegameSafetyHarness.cpp).
- Base: origin/master 64c792a61c4b5a7cdbfc49f33e7c846127645056; explicitly rebased, fetched again before final validation.
- Linux x86_64, Ubuntu GCC13.3.0, kernel7.0.0-38-generic. Release O3, ordinary compilation, no fast/PCH/unity build; pinned project dependencies and SDL3 prefix. No runtime dependency added. Linked libraries and Python environment are in the evidence archive.
- Build: GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix scons release=1 --build=build/gradient-official -j4 build/gradient-official/src/glob2 engine-tests unit-tests
- Native engine: python3 test/run_tests.py --build-dir build/gradient-official --binary engine --filter 'ResourceGradientPolicy/*' --filter 'MapGradientInvalidation/*' --filter 'GradientPreparation/*' --filter 'SharedWorkerLifecycle/*' --filter 'SimulationReadPhase/*' --filter '*Stats*/*' --filter 'MarketsV2/*' --filter 'TurnEngineHarness/*' --filter 'RuntimeResources/*' --filter '*Safety*/*' --filter '*Terrain*/*' --filter '*Replay*/*' --filter '*Untrusted*/*' --jobs 3. Full JUnit/log retained. 292/296 passed on the production commit. Four stale test assumptions were fixed in the test-only follow-up and all four passed on rerun (0 failures); the initial failures remain in the evidence for audit.
- Focused units: python3 test/run_tests.py --build-dir build/gradient-official --binary unit --filter '*Gradient*/*' --filter '*Metric*/*' --filter '*Replay*/*' --filter '*Save*/*' --filter '*Version*/*' --jobs 3:190 cases passed, exit0.
- Python: python -m unittest tools.test_resource_gradient_model:7 passed, exit0; includes4100 C++/Python predictor parity vectors under UBSan, heldout/grouped fit, balancing, manifest SHA and resume tests.
- CLI: python3 tools/cli_reference.py --binary build/gradient-official/src/glob2 --check:exit0.
- Standalone C++ policy UBSan:3 cases/2658 assertions passed; binary/text roundtrip, rolling windows and overflow ages. macOSARM64 and Linuxx86_64 policy/model trace identical across8192 ticks/629 refresh decisions. Sources and traces retained.
- Real matches: Isles64duel seed7801 and River128four-playerFFA seed7802,8192ticks each, Nicowar. Probe off/1thread, probe on/1thread, probe off/4threads:deep world, snapshot and replay checksum sidecars identical byte-for-byte for each map. All commands, hashes and results retained. Final checksums3533878747 and149697285 respectively.
- Real save continuation: River checkpoint4096ticks with1thread, reload with4threads through8192;4096overlapping deepchecksums (4096..8191),0divergences. Save retained.
- Probe workflow smoke:four independently seeded River128four-playerFFA games8192ticks; permanent run/fit/emit succeeds with grouped holdout and compressed hashed dataset. This small smoke fit is NOT a replacement for the committed selected model and not a quality/performance estimate.
- Golden update: current simulation match record/trace and current seeded resource composition trace regenerated and verified. Historical market and old save/statistics goldens unchanged and passed.
- Independent fresh code review:approved after fixing team-key cleanup, cached checksum histories, old-save publication initialization, capacity bounds and test adaptations. Final independent review: OK; all findings resolved, with platform qualification omissions explicit.
- Limits:full macOS/Windows/browser engine checksum equivalence, other toolchains, full native inventory, shipping packaging and manual gameplay have not been run. macOS activity was deliberately limited to isolated model/policy parity because the user needed that machine. GPU pipeline unit fakes and Linux rendering tests do not establish full platform or GPU gameplay qualification. No hosted affected/full CI requested. Keep PR draft pending platform qualification and maintainer gameplay/acceptance; no maintainer acceptance is claimed.

Evidence layout

- games-evidence.tar.gz: maps, per-run commands/results/logs, all deep-world and snapshot hashes, representative ordinary replays/checksum sidecars, save/resume evidence, collector episodes and four-game training smoke. Identical observed/parallel replay copies and partial checkpoint/resumed replays are omitted to avoid duplicate bulk; comparison SHA256 results remain in equivalence.json. Full originals remain on devlaptop.
- native-evidence.tar.gz:native JUnit/logs and screenshots, build logs, dependency/provenance information, CLI and Python outputs.
- policy-parity*.trace and policy-parity.cpp:portable integer policy/model test.

These are development evidence on a dedicated branch, not files shipped in the feature PR. No CPU-saving estimate is inferred from instrumented validation games.

Final four reruns: python3 test/run_tests.py --build-dir build/gradient-official --binary engine --filter "TerrainProperties/terrain edits refresh*" --filter "TerrainRuntime/production pipeline*" --filter "TerrainRuntime/embedded registry*" --filter "SavegameSafety/*" --jobs 2;4passed,0failed.
