# Native lobby automation on macOS

During the custom-game redesign, desktop automation moved the mouse pointer to
Glob2 controls but clicks did not activate them. Screenshots and application
activation worked, so pointer movement alone was not evidence that the game
received an input event. Repeated attempts and requests for manual clicks did
not provide dependable interaction coverage. The exact macOS injection failure
was not established; do not assume that this is a game UI defect or a specific
permission failure.

## Reliable approach for this SDL application

Use `CustomGameSetupHarness` to drive the **production SDL event loop** with
`SDL_PushEvent`. Queue mouse down/up pairs and keyboard events, run the real lobby
and nested profile screen, then assert the resulting setup and launched game
state. Render screenshots with `dispatchPaint` / `printScreen` from the actual
native widgets. This exercises compiled application code, not an HTML mockup.

From the isolated worktree:

```sh
scons -j8 release=1 custom-setup-test build/src/glob2
build/src/CustomGameSetupHarness
mkdir -p artifacts/visual-pass/compact artifacts/visual-pass/large
build/src/CustomGameSetupHarness artifacts/visual-pass/compact
build/src/CustomGameSetupHarness artifacts/visual-pass/large large
build/src/CustomGameSetupHarness artifacts/visual-pass/compact ui
```

Use deterministic interaction assertions for selection, scrolling, dropdown
cancellation, controller assignments, rules and preview ownership. Keep random
map generation random; assert invariants, not a particular terrain image.
A bounded watchdog should fail a stalled event-loop test rather than leave a
modal screen open indefinitely. Timer callbacks should enqueue events only;
rendering and game state changes belong on the main thread.

## What this does and does not verify

SDL injection verifies game-side input handling, layout, rendering and launch
behavior. It does not verify delivery of physical mouse/keyboard input through
macOS, accessibility permissions, Retina/window coordinate translation, or
bundle relocation. Separately smoke-test the packaged app's launch and inspect
its window. If desktop injection fails again, switch promptly to SDL automation
and report the remaining platform-input coverage honestly.

Random previews now run automatically after a 500 ms edit debounce. The UI driver
allows the timer to run after selecting Random; it must not click a Generate
button. The control tests also drive the timer before/after its deadline and
while a slider is held, and check that a failed attempt is consumed.
