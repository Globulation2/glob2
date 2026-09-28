# PR 208 merge preparation evidence

Branch under review: `codex/mobile-platform`; preparation revision `235924e4f`.

## Current host checks

`latest/` contains the current macOS CppUnit (203 cases), real touch/editor,
responsive menu, mobile presentation, portable renderer, translation/font,
UTF-8 layout, build-system and browser unit checks. The focused browser run
retains its three failures; `browser-corrections.log` records all nine targeted
reruns passing after correction. The full browser sweep and final CI are linked
from the PR when complete. Host screenshots are not physical-device captures.

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
