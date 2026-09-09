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
  unique iOS archive symbols and successful simulator LLDB attachment.

See the newest status checkpoint for actual test results; CI wiring does not by
itself mean a hosted CI run passed.

## Continue without physical devices

- Run and fix hosted Android ARM64/ARMv7/x86-64 packaging and iOS simulator CI.
- Qualify iOS picker interaction and third-party/cloud providers. Both native
  document bridges are implemented; Android local-provider round trips pass.
- Qualify mobile WSS gateway traffic, mixed-platform play and coordinated
  reconnect. Platform certificate trust is integrated; native framing/queues
  continue through the existing bounded Beast transport.
- Extend the passing 100,000-step macOS ARM64/Wasm fixture to physical mobile
  performance and additional seeds as device coverage becomes available.
- Qualify debugger attachment, symbols, sanitizer/profiler workflows and clean
  build reproducibility. Audit remaining host tools against pinned versions.
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
