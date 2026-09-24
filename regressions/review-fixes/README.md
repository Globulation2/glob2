# Review fixes at 262e63ec0

All three review findings have regression coverage. Production changes do not alter simulation orders, state, save formats, or protocol versions.

- Settings: the new delayed-write success/failure cases fail on the prior browser build. All 21 settings tests pass on rebuilt software-rendered Chromium, Firefox and WebKit. Run `GLOB2_TEST_RENDERER=software npx playwright test settings-storage.spec.js --project=chromium --project=firefox --project=webkit` from `browser/`.
- TCP: the 200-message one-way loopback burst fails before the transport fix and passes afterward. Build `scons release=1 transport-test` and run `python3 test/run-network-transport-tests.py`. The separate 300-message benchmark takes 3.657 seconds before and 0.013 seconds with the send-delay control. Its C++ source and control implementation are included; compile with C++20, `-DYOG_SERVER_ONLY`, `-Isrc/net` and SDL2/SDL2_net pkg-config flags, linking either `src/net/NetTransport.cpp` or the included control.
- Native polling: WindowResizeHarness now runs the real application host and verifies resize reconciliation before frame dispatch. The test fails with direct SDL polling and passes with GraphicContext polling. Build `scons release=1 resize-test`, then run WindowResizeHarness software with `SDL_VIDEODRIVER=dummy`. The existing cached-frame checks also pass. Actual Windows modal dragging was not repeated locally.
- ScreenExecutionHarness and native SettingsScreenTest pass. The native settings fault injection now obstructs the destination rather than relying on an obsolete temporary filename. Its quick case is included in Linux CI.

The settings-race script and screenshot capture the original bug; its relative imports expect the script in artifacts/pr379-review in the source checkout. Logs retain the injected failures and missing-translation diagnostics from the native settings harness.

CI for the updated head: https://github.com/Globulation2/glob2/actions/runs/36061582543

The follow-up commit keeps server-only ApplicationHost polling independent of graphics. The complete standalone test build, TestsRunner, and CampaignLoadHarness pass locally; the client resize and screen harnesses still pass.

The rebuilt macOS client at `8fd3e8e2a` produces the same 1,500-tick trace as the master baseline: `110443ab4a136dff6621f32dbeed150a15c18394791874e3774ef6cbe27c3c48`. Compressed trace, replay, and manifest are retained here.

The final commit corrects the session-load test to retain the named save instead of selecting the first row, which can become an autosave. The damaged-load test now waits past the initial autosave. Restoring the old first-row selection fails with a successful match instead of the expected error; all three corrected WebGL session tests pass locally with SwiftShader. Logs are included.

Final-head CI replay artifacts are retained in `final-ci-determinism.tar.gz`. Both Linux toolchains, Windows, and WebAssembly produce the master-baseline checksum hash above.
