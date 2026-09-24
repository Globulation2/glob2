# Browser platform review evidence

Evidence for [Globulation2/glob2#379](https://github.com/Globulation2/glob2/pull/379), rebased onto master `a494a331929c6104c44be1bf395ef1c445bbd4c2`. Runtime build revision, review head, and scenario hashes are in [checksums/manifest.json](checksums/manifest.json). This branch contains review artifacts only; it is not part of the implementation diff.

## Simulation and compatibility

The master macOS arm64 build, rebased native macOS arm64 build, and compiled WebAssembly client produced identical binary per-tick traces for `games/cross-replay.game`, seed 42, 1,500 ticks. Each decompressed trace has SHA-256 `110443ab4a136dff6621f32dbeed150a15c18394791874e3774ef6cbe27c3c48`. The final aggregate checksum is `16791a66`.

[checksums/](checksums/) contains all three compressed traces, the initial saved game, and the native replay. `browser-export.replay.gz` is a separate recording saved through the browser UI, persisted across reload, and opened through the replay chooser. To reproduce native output from a built checkout:

```sh
python3 test/run-browser-determinism.py build/darwin/client/release/src/glob2 artifacts/determinism/native
```

For WebAssembly, build and serve the release as described in `browser/README.md`, then run `npx playwright test tests/determinism.spec.js --project=chromium` from `browser/`. The native traces from CI run 36039216063 also match byte for byte on Linux with GCC 11 and 13 and Windows. Their compressed traces, manifests and run logs are included here. The final CI comparison also includes the Linux-built WebAssembly trace; consult the PR checks for its status. These checks establish this scenario, not every possible simulation or generator.

Save safety and five save/continuation checkpoints of 256 ticks each passed. Network admission tests cover old/current/future versions. Replay export preserves every recording byte and trailing idle ticks, including memory-backed recordings. Recording files use binary mode on Windows. Save and replay floors are unchanged from master; multiplayer requires protocol 41 on clients and YOG servers.

## Reproduced regressions

- [screen-before.log](regressions/screen-before.log) fails the queued-transition assertion when `ScreenStack::frame()` draws a parent after a child has changed the frontend style. [screen-final.log](regressions/screen-final.log) passes the lifecycle suite with the pending-transition draw guard. Build with `scons release=1 screen-test` and run `build/darwin/client/release/libgag/src/ScreenExecutionHarness` with a disposable `GLOB2_USER_DIR`.
- [preview-restart-before.log](regressions/preview-restart-before.log) records AddressSanitizer's container overflow when a worker publishes to a preview slot removed by `restart()`. [preview-restart-after.log](regressions/preview-restart-after.log) and [exit codes](regressions/preview-restart-result.log) show success with the bounds check. The `CustomGameSetupHarness preview-restart` case exercises this boundary. This check instrumented `LandscapePreviewer.cpp` and linked the existing release harness with ASAN; it was not a whole-program sanitizer run. Reproduce the failing version by removing only `index >= passes.size() ||` from the result-publication guard.

- [replay-export-before.log](regressions/replay-export-before.log) fails the trailing-idle-tick check with the old export. [replay-native-test.log](regressions/replay-native-test.log) passes round trips for file and memory recordings with the corrected copy and terminator. Build with `scons release=1 session-test` and run `python3 test/run-engine-session-test.py`.

## Browser and native checks

The [tests/](tests/) directory retains raw test output. The final Chromium suite passed 91 tests with one intentional skip (live interface scaling is software-only); Firefox and WebKit passed 28 focused checks. The same scaling behavior passed in their software-renderer runs. The full final Chromium run uses macOS Metal WebGL2; the Firefox and WebKit startup, menu, cursor and viewport checks use the software renderer. Focused torus checks exercise actual WebGL context loss and restoration on both Metal and SwiftShader. The emulated-GPU transition allowance is two minutes; these results do not establish hardware-like performance on SwiftShader. WebKit automation does not establish shipping Safari compatibility. Linux WebKit automatically selected WebGL2 and intermittently failed high-density resizing in CI; a local Linux GPU run also had a graphics-process exit. The explicit Linux WebKit software suite passes all 10 checks. Use `?renderer=software` if affected; passing software results do not validate WebKit GPU operation.

[screenshots/](screenshots/) shows the modern colony menu background, the custom-game panel, and the campaign menu after returning from a match. The legacy grass/cloud menu renderer has been removed. Actual map terrain and optional gameplay clouds remain part of the game.

Later review-head changes correct native test commands, the explicit protocol assertion, developer-tool build paths, installation dependencies, CI renderer selection and time allowance, and visibility-test navigation. Docker excludes temporary review artifacts. The tested game/runtime source is unchanged. The complete Maxima implementation runner and team-statistics compatibility harness also pass locally.

Native validation includes screen and session ownership, map preview and custom setup, save safety and unit continuation, protocol admission, TCP/WSS transport, gateway tests, shared build layout, Maxima lifecycle/structural tests, and tournament CLI regressions. Native client, server/router, gateway and WebAssembly release builds completed locally. Both real TLS deployment tests pass with a freshly built Linux arm64 image, including private routes and account persistence across recreation. Corrected real-window visibility tests pass on macOS in software and WebGL2 modes. A Linux arm64 container run of the raw-window test crashed before its assertions; use CI for Linux x86-64 coverage.

Independent maintainer review and human playtesting remain needed for the shared screen lifecycle and presentation changes. These artifacts do not constitute approval to merge. Website deployment, mobile controls, and reconnect recovery are outside this PR.
