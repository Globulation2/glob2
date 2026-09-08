# Viewport resize contract

The browser host retains the newest viewport dimensions until the application
consumes them at a frame boundary. Both software and WebGL2 use this event.
WebGL2 updates its drawable and projection without replacing the context. The
software renderer allocates a replacement
surface before releasing its previous surface; nonpositive dimensions and failed
allocations leave the old target intact. Desktop hosts do not request automatic
resolution changes, preserving their existing video settings.

A successful resize updates the target, clipping and all retained or pending
screens before processing that frame's input. Overlay screens recenter. Game and
editor screens move their camera to preserve the center map tile, update minimap
drawing and hit regions, and clear held gestures. The game session retains its
simulation and selection. Editor controls anchored to the right or bottom move
with their respective edges. The session resets its timing baseline so time spent
behind the minimum-size notice does not become simulation catch-up work.

Rendering uses one pixel per CSS pixel, including displays with a device scale
factor of two. Below 800 by 600, an in-game notice covers the retained screen;
restoring a usable size resumes that screen. The page has no permanent wrapper
controls.

## Verification

`browser/tests/viewport.spec.js` uses real browser resize and mouse/keyboard input
in Chromium, Firefox and WebKit. Set `GLOB2_TEST_RENDERER=webgl2` to run these
same scenarios against the GPU backend. It checks canvas dimensions and its page bounds,
nonblack rendered pixels, menu hit positions, ongoing simulation, the minimum-size
notice, editor discard dialogs, and high-density displays. Screenshots capture the
resized game menu and the notice. The native engine-session harness checks actual
software presentation, rejected zero-sized targets, camera-center and simulation
checksum preservation, and minimap hit regions after resizing.

## Remaining release work

This implementation covers scheduled application screens with software and
WebGL2 rendering. Legacy blocking multiplayer flows defer application resize handling;
those flows must migrate to the screen stack. WebGL2 context restoration is
covered by the rendering suite; its ownership and remaining qualification are
recorded in [ADR 006](adr-006-webgl2-rendering.md). Camera coordinates retain the existing
whole-tile precision. Broader selection/dragging and nested-dialog coverage is
still needed, along with browser video-preference policy, hidden-tab lifecycle,
and coordinated multiplayer suspension. Very narrow notice layouts and allocation
failure messages need further work. These limitations keep the platform
experimental; this document does not qualify the full release requirements.

## Visibility lifecycle

The browser retains visibility edges until the application consumes them, even
when callbacks did not run while hidden. Scheduled screens suspend input on both
edges; the application skips their update while hidden. A session resumes on its
logical clock, preserving the pending tick deadline and excluding elapsed hidden
time. The native regression inserts a 60-second gap and verifies the resulting
50-tick session. `browser/visibility.config.js` runs a separate real-window
Chromium regression without Playwright focus overrides; on Linux run it under
`xvfb-run -a`. Ordinary headless tests do not prove document visibility behavior.
Firefox/WebKit real-window qualification and coordinated multiplayer suspension
remain required.

## Button coordinates in the pinned SDL browser backend

SDL 2.32.8's Emscripten mouse-button callback uses SDL's last motion position,
rather than the button event's coordinates. A missing/coalesced motion can
therefore make a valid click hit the old position. The browser shell synchronizes
absolute motion from each button event before SDL's button listener runs. It
handles releases outside the canvas for a canvas-started press and skips relative
pointer-lock input. This SDK adaptation stays in the browser platform layer.

`browser/tests/input.spec.js` suppresses trusted motion delivery while keeping
real button input, then selects a map, starts a match, resizes and quits it.
The regression fails before the adapter in Chromium and passes afterward in
Chromium, Firefox and WebKit. The resumed real-window Chromium checks also pass
with both renderers; see the dated evidence in [status](status.md).

## Browser navigation shortcuts

The browser shell reserves Ctrl/Cmd+R (including Shift for reload variants) and
Ctrl/Cmd+L for the browser while the canvas has focus. Capture-phase handlers
stop these keydown/keypress events before SDL can cancel their default action.
Key releases still reach SDL, avoiding a stuck game key if it was already held
before the modifier was pressed. Alt-modified combinations remain game input.
Desktop input is unchanged.

The input suite checks default-action cancellation against the real SDL
listeners in each browser engine, then verifies ordinary menu input. Synthetic
keyboard events cannot trigger browser chrome, so actual shortcut navigation
also requires manual browser verification. Reload is ordinary page navigation;
it does not promise to save an unsaved match or finish a pending storage write.
