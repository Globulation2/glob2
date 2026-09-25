Latest desktop regression follow-up: [desktop-regression/README.md](desktop-regression/README.md).

Current responsive extension evidence is in [responsive/README.md](responsive/README.md).
The original mobile-port evidence below describes its earlier source revision.

# PR 208 mobile verification evidence

Source: `458dc357968118d30ee8030b1dcc3227dd66c022` (the simulation/UI port began at `2c05b4d86`; the final commits fix translations, toolchain bootstrap and phone layouts).

- Xcode 27.0 / iOS 26.5 / ARM64 iPhone 17 Pro simulator: release build, bundled resources, startup, retained-process background/resume, termination and fresh-process relaunch passed. See `lifecycle-result.json`.
- Same initial save, seed 42 and orders: macOS ARM64 and iOS ARM64 simulator have byte-identical per-tick checksum sidecars for 1,500 ticks (47 orders), final checksum `16791a66`. The input, replays, sidecars and logs are in `simulation-evidence.zip`; `checksum-result.json` records the SHA-256.
- The desktop-generated replay loads into the iOS game session. `replay-portrait.png` shows playback.
- Interactive iPhone checks before the host locked: setup tabs, landscape gameplay, construction drawer, building worker allocation and priority, and rotation back to portrait. The save dialog opened the iOS keyboard and kept actions above it (`save-portrait.png`). Save-name entry/completion, pinch gestures and document import/export were not completed.
- The AI profile safe-area/overlapping Back button and misleading desktop-only settings were fixed after this playtest. The rebuilt iOS app passed lifecycle checks; automated native portrait/landscape presentation tests passed, including non-overlap and touch-size checks for Back/Use. `native-phone-layouts.zip` contains the harness screenshots (macOS portable renderer, not iOS captures).
- `presentation-test.log` and `build-system.log` contain the native regression results. Native presentation logging includes existing missing settings-help lookup warnings; the test itself passes.

Physical Android/iPhone playtesting and Android/Windows simulation checksum comparison remain unverified. Build CI is linked in the PR separately. These artifacts support review; they do not claim exhaustive device qualification.

Reproduce the simulation runs with `GLOB2_REPLAY_PATH=... GLOB2_CHECKSUM_SIDECAR=1 glob2 --nox cross-replay.game 1500 1`. The simulator uses the same arguments via `simctl launch` and `SIMCTL_CHILD_` environment variables. For replay playback, copy the replay into the application's writable `replays/` directory and use `-replay replays/desktop.replay`.
