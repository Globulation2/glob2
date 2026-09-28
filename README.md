# PR 208 merge preparation evidence

Branch under review: `codex/mobile-platform`; final preparation revision `ddef13590`.

## Current host checks

`latest/` contains the current macOS CppUnit (203 cases), real touch/editor,
responsive menu, mobile presentation, portable renderer, translation/font,
UTF-8 layout, build-system and browser unit checks. The focused browser run retains its three failures; `browser-corrections.log`
records all nine targeted reruns passing after correction. The primary Chromium
sweep covers 96 scenarios across `browser-full.log` (71 passed) and
`browser-remaining.log` (25 passed). The first sweep and overlapping WebGL run
had timeouts and were interrupted; their logs are retained. The corrective runs
used Chromium ANGLE Metal serially on macOS, avoiding concurrent GPU contention.
Firefox/WebKit coverage adds 28 passing tests (`browser-cross.log`).

`browser-final.log` records 28 passing WebGL tests and one intentional software-only
scale skip; that test passed in the software run. This final run includes named
Swamp/Concrete islands generation cancellation and TCP/WSS native/browser matching
simulation checkpoints. Raw checkpoint files, observed order checksums and alignment
metadata are in `browser-attachments/`. Screenshots are host browser captures,
not physical-device captures. The native/browser executables were rebuilt after
merging master and restoring the results Enter shortcut; subsequent preparation
commits change tests, translations and language-completion metadata, not C++ rules.
Latest hosted CI status is on the PR; these local results do not establish that
all final hosted jobs passed.

## Earlier device and design evidence

- `android/text/` shows physical Samsung SM-A065M / Android 16 text sharpness
  before/after `2ae1a0e84`, plus its real-device touch harness result.
- `android/native-tests/` retains the original combined run, including failures,
  and the final successful reruns of responsive menus, presentation, and engine
  sessions. Together these establish 203 CppUnit cases and eight native harnesses
  passing on that Android CPU during the startup-fix work (`85f73a646`). They use
  SDL dummy drivers, not JVM/Activity UI automation. These are earlier revisions,
  not a claim that the final merge-preparation commit was installed on hardware.
- `gallery/` contains host-rendered overview comparisons from the gameplay/editor
  redesign (`3ccc32756`). `editor/` records independent design feedback and closure.
- `gameplay/` shows the compact flag row and inspector after `7362a0130`.
- `zoom/` records fractional-zoom border verification from `d9afbbdde`.
- `flags/` verifies the touch-only selection correction from `fc1c4d938`.

Real iOS hardware and comprehensive platform keyboard composition remain outside
this evidence. The maintainer's subsequent Android playtesting is reported in the
PR separately from automated checks. No new simulation/save-format change is
introduced by this branch relative to its merged master base.
