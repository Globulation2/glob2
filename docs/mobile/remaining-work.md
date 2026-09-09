# Remaining mobile implementation and qualification

The user authorized continuing all remaining implementation work while Pixel 6
feedback is unavailable. Keep using this worktree and the existing mobile branch;
do not modify the AI or browser checkouts. Commit and push verified checkpoints.

## Implemented in the current continuation

- Native atomic file/directory synchronization with failure and process-death tests.
- Two checked single-player recovery generations, startup recovery/discard/later,
  full-load fallback, campaign context, initial/background/periodic checkpoints,
  and final-save failure handling. Native recovery is opt-in in desktop harnesses.
- Android emulator smoke runner and CI wiring, pinned emulator/image downloads,
  Linux SDK tools and versioned Python build drivers.
- iOS simulator smoke runner and CI wiring using Xcode 26.6/iOS 26.5.
- Native document bridges, Android picker round-trip/cancel/error verification,
  and SDK metadata registration for directly extracted emulator archives.
- Platform certificate trust in bounded native WSS, Android trust instrumentation,
  unique iOS archive symbols, simulator LLDB attachment and symbolicated sampling.
- Bounded abandoned-write/export cleanup and host sanitizer checks.
- A repeatable 100,000-step ARM64/Wasm fixture with 101 matching checkpoints.
- Reconciliation with browser checkpoint 1658ff670 and isolated cross-play ports.
- Android native asset-preparation feedback, verified in a real emulator cache
  reinstall and followed by a passing lifecycle/rotation/relaunch smoke.

See the newest status checkpoint for actual test results; CI wiring does not by
itself mean a hosted CI run passed.

## Continue without physical devices

- Hosted mobile run 34315653259 is fully green: all Android ABIs package,
  hosted x86-64 lifecycle/trust and iOS lifecycle checks pass at `b9806743d`
  without retries. Build-ID packaging checks pass on all three Android ABIs.
  The general browser/desktop workflow is still running for that commit.
- iOS picker presentation, browsing, cancellation and reopening now pass through
  task-local idb input on the isolated simulator. Its local File Provider cannot
  resolve selected files (including plain text) and disables export Save, even
  after reboot. Import/export round trips remain unqualified; see
  [simulator interaction](simulator-interaction.md). Third-party/cloud provider
  testing also needs providers/accounts. Android local-provider round trips pass.
- Mobile WSS end-to-end qualification needs a trusted gateway endpoint and
  mobile participants. Native/Wasm TCP and WSS cross-play is covered locally;
  mobile certificate trust passes separately. Coordinated device reconnect stays open.
- Extend the passing 100,000-step macOS ARM64/Wasm fixture to physical mobile
  performance and additional seeds as device coverage becomes available.
- Physical debugger/sanitizer/Instruments workflows and a complete host-tool
  lock remain. Simulator LLDB/symbols/sampling, host harness sanitizers and clean
  hosted packaging are verified; byte-identical distribution archives are not.
- Address accessibility/localization gaps supported by automated or simulator
  checks; preserve honest physical assistive-technology qualification gates.
- Recovery follow-up: storage-exhaustion/process-termination checks on mobile
  hardware. Bounded stale-file and iOS export-staging cleanup is implemented. Recovery
  currently applies to single-player games, not editor drafts or network sessions.

## Requires devices, credentials or external participation

- Pixel 6 touch/layout, keyboard, audio interruption, rotation and save latency.
- Signed iPhone/iPad installation and real-device lifecycle/graphics restoration.
- Older 2 GB Android/ARMv7 and iPhone SE-class testing.
- 30-minute thermal/frame-time/memory qualification and cross-device multiplayer.
- Apple signing identity/provisioning and any distribution credentials.

Pinch zoom, a separate main-menu visual redesign, full mobile-browser delivery,
voice/cloud saves and store submission remain explicitly deferred from the current
scope. Do not report the full mobile release complete while the gates above remain.
