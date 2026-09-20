# Custom rule preference regression evidence

Validated on macOS with `scons -j6 release=1 server=0 custom-setup-test`.
The harness uses its own `glob2-custom-setup-tests` profile.

- `before.txt`: added rule-restoration assertions against the original preferences
  implementation, before applying the fix. Exit 134: economy rules were lost.
- `headless.txt`: `build/src/CustomGameSetupHarness`, exit 0 after the fix. Includes
  format 1/2 migration, format 3 round trips, invalid/truncated data rejection,
  generator controls, and the existing real-engine save/replay checks.
- `preferences-write.txt` and `preferences-read.txt`: separate processes running
  `build/src/CustomGameSetupHarness preferences-write` and `preferences-read`,
  both exit 0. All thirteen additional rules survive disk storage and reopening.
- `visual.txt`: `build/src/CustomGameSetupHarness artifacts/custom-rules-ui`.
  The rule-control assertions pass, including preserving No upgrades in both
  states when Map knowledge changes. The broader visual run subsequently stops
  at the landscape preview zoom-anchor assertion (exit 134).
- `interactive-launch.txt`: optional `build/src/CustomGameSetupHarness
  /tmp/maxima-test-diagnosis/ui ui` run. Stops at the existing expected Nicowar
  player assertion (exit 134); this launch-driver path was not changed.

The standard CI custom-game job already builds this harness and runs the headless,
visual, and separate-process preference checks. These local results do not claim
cross-platform simulation equivalence. The patch changes lobby preferences only;
it does not change engine simulation or save/replay serialization.
