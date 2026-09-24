# Review fixes at 30aa48d12

All three review findings have regression coverage. Production changes do not alter simulation orders, state, save formats, or protocol versions.

- Settings: the new delayed-write success/failure cases fail on the prior browser build. All 21 settings tests pass on rebuilt software-rendered Chromium, Firefox and WebKit. Run `GLOB2_TEST_RENDERER=software npx playwright test settings-storage.spec.js --project=chromium --project=firefox --project=webkit` from `browser/`.
- TCP: the 200-message one-way loopback burst fails before the transport fix and passes afterward. Build `scons release=1 transport-test` and run `python3 test/run-network-transport-tests.py`. The separate 300-message benchmark takes 3.657 seconds before and 0.013 seconds with the send-delay control. Its C++ source and control implementation are included; compile with C++20, `-DYOG_SERVER_ONLY`, `-Isrc/net` and SDL2/SDL2_net pkg-config flags, linking either `src/net/NetTransport.cpp` or the included control.
- Native polling: WindowResizeHarness now runs the real application host and verifies resize reconciliation before frame dispatch. The test fails with direct SDL polling and passes with GraphicContext polling. Build `scons release=1 resize-test`, then run WindowResizeHarness software with `SDL_VIDEODRIVER=dummy`. The existing cached-frame checks also pass. Actual Windows modal dragging was not repeated locally.
- ScreenExecutionHarness and native SettingsScreenTest pass. The native settings fault injection now obstructs the destination rather than relying on an obsolete temporary filename. Its quick case is included in Linux CI.

The settings-race script and screenshot capture the original bug; its relative imports expect the script in artifacts/pr379-review in the source checkout. Logs retain the injected failures and missing-translation diagnostics from the native settings harness.

CI for the updated head: https://github.com/Globulation2/glob2/actions/runs/36054885321
