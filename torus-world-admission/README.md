Verification for PR #1205

Source 61cd188ec (full SHA in verification.json), base a5b380584bb3958488f01adba274e2f9dd6dce21. Ubuntu24.04 x86_64/GCC13.3, release=1 (-O3 -s), existing pinned SDL3.4.16/codecs. DISPLAY unset: each display case owns its declared Xvfb/Openbox screen; Vulkan explicitly uses /usr/share/vulkan/icd.d/lvp_icd.json (Mesa25.2.8). Exact build commands, flags and clean source provenance are retained.

Observed failure: the prior complete clean native run finished 1872 jobs/2968 cases, with 1871 jobs passing and one SDL GPU torus overlay preview failure. Concurrent probes reproduced repeated preview advancement. A diagnostic-only test build (only added warm-up counters and actual-highlight reporting) confirms a correct warm-up world submission and highlight strength 0.4 after recovery; this disproves the initial warm-up-budget hypothesis. No warm-up retry limit was changed.

Deterministic regression: extend the existing overlay-admission wrapper to reject one admitted final world submission as well. With unchanged production code at a5b380584 and only this test edit, OpenGL fails in 2.5s: actual highlight 0.2 instead of 0.1. The production capture is released after composing overlays, before the caller knows whether world submission succeeds; a deferral therefore starts another capture and advances the preview again. The original dirty-source provenance explicitly records only the test edit. Regression source/log/JUnit are retained.

Repair: retain the completed atlas and frozen frame with captureReady until world submission is admitted. Retry the world mesh against the same atlas without recapturing regions or overlays. Explicitly flush world work before releasing capture ownership. Map/team/options invalidation still initializes a new capture. Animation source phases retain their existing per-turn progression. The forced overlay/world deferrals, backend identity, map checksum, source animation and exact single-preview-step assertions all remain.

Intended visible effect: preview opacity no longer jumps ahead while final world admission is deferred. This adds an explicit submission boundary before later HUD work; no performance improvement is claimed. No simulation, save-state, replay/network version or dependency changes. Capture state is transient renderer state, not serialized.

Validation at the clean final source:
- GLOB2_SDL3_PREFIX=<existing-pinned-prefix> GLOB2_GLSLANG=<pinned-glslang15.1> scons --build=build/ci-recovery-release release=1 -j6 engine-tests unit-tests: passed.
- env -u DISPLAY VK_ICD_FILENAMES=<lavapipe> python3 test/run_tests.py --build-dir build/ci-recovery-release --binary engine --filter 'TorusRender/*retries a submitted final*' --filter 'TorusRender/*fatal torus*' --fullscreen --display-jobs 1 --junit <retained>/tests.xml: all four GL/SDL retry and fatal-fallback cases pass (34.0s), including the deterministic regression.
- Same runner, filter 'TorusRender/*captures large logical maps*': both GL/SDL cases pass (39.6s), retaining logical-map captures at 4096,2048,1024 limits.
- Four SDL GPU retry probes, two concurrent workers, same Lavapipe/Xvfb: 4/4 pass, 31.88/39.70/31.68/33.23s. Raw outputs and source provenance retained.
- python3 tools/check_docs.py: 309 documents/zero errors.
- python3 test/check_sim_revision.py --base origin/master: passed.

Coverage rationale: actual GL and SDL implementations, asynchronous admission, forced failures after both final overlay and world submission, large targets and fatal fallback are directly affected. Prior full suite passes every other case. A final full local rerun and latest-master full hosted matrix continue. Metal, D3D and browser hardware are not locally verified; no complete platform or performance claim is made from these focused cases.
