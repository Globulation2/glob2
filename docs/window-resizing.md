# Window resizing

Windowed mode follows the OS window dimensions when `RESIZABLE` is enabled
(`-r`; `-R` disables it). SDL enforces the minimum 640 x 480 game size. Desktop
fullscreen retains its selected logical resolution and aspect-preserving scaling.
Windows requires SDL 2.30 or newer; startup checks the runtime version.

## Event and rendering ownership

SDL window operations, the GL context, simulation, and rendering stay on the main
thread. All GUI event loops use `GraphicContext::pollEvent`. Only while that method
is inside SDL's event pump may an exposed-event watcher present a cached frame.
The watcher checks its thread and window and rejects recursive presentation. It
never enters screen painting, timers, animation updates, or simulation.

SDL 2.30's [Windows modal resize support](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.30.0/src/video/windows/SDL_windowsevents.c)
sends exposed events while the OS holds the main thread in a move/resize loop.
The cached image is scaled during that interaction. Once polling returns, the
renderer queries the latest OS dimensions and updates the logical surface,
projection, viewport, and clipping. It does not call `SDL_SetWindowSize`, recreate
the GL context, or reload sprite textures in response to a size notification.

The GL path copies the completed back buffer into a persistent power-of-two
texture before swapping. This supports the legacy renderer without requiring an
FBO extension. Its texture wrapping also supports Windows' GL 1.1 GDI renderer.
Cached presentation saves/restores GL attributes and matrices, so
the engine's state cache remains valid. If a frame exceeds the device's maximum
texture size, cached GL presentation is unavailable; normal rendering continues.
The software path owns its logical drawing surface and caches a completed copy;
it reacquires SDL's window surface for each presentation. Neither backend depends
on the default back buffer or SDL-owned surface surviving a resize.

Watchers are removed and frame caches discarded before window/context destruction
or recreation for explicit settings changes. No presentation resources are created
in headless or dedicated-server mode.

## Layout

Editor controls retain their distance from the right edge of the logical screen.
Their shared rectangle is updated before both drawing and hit testing. Generic map
rectangles remain absolute and keep their half-open edges. The minimap refreshes
its geometry before painting, hit testing, and coordinate conversion.

Overlay dialogs are constrained to their parent before painting and input dispatch.
Modal backdrop images scale to cover the resized parent. A dialog larger than the
minimum screen can still require a larger window; this change does not redesign
fixed-size dialog contents.

`MapEdit::draw(frameTick)` assembles one normal editor frame, including dialog
timers and drawing with animation side effects. It is called once per normal
frame and is not an expose callback.

The editor anchoring, minimap positioning, and frame extraction adapt Nathan Mills
(@Quipyowert2)'s work in [PR #64](https://github.com/Globulation2/glob2/pull/64).

## Timing and validation

Simulation pauses while Windows holds the main thread in its modal loop. On
return, the existing engine pacing limits catch-up debt to `MAX_CATCHUP_MS` (500
ms). Cached presentation does not advance clouds, particles, dialog timers, or
cursor animation. Continuous simulation during a drag would require a separate
state-ownership design. Prolonged resizing may stall a network match; multiplayer
timeout behavior still needs a Windows play test.

Build the integration harnesses with `scons resize-test aspect-test`. Run
`build/libgag/src/WindowResizeHarness` and `FullscreenAspectHarness` with `software`
and `gl`. The resize harness checks cached pixels after incomplete drawing,
callback guards, normal-frame counts, GL state restoration, grow/shrink reflow,
context identity, input coordinates, minimum dimensions, and cache invalidation
on window recreation. OpenGL pixels are captured at the swap boundary rather
than reading the post-swap front buffer, which is unreliable under Mesa/Xvfb.
Linux CI runs both backends under Xvfb/Mesa.

The resize harness passes in a Windows Server 2022 desktop VM with SDL 2.32.10,
using both software rendering and OpenGL 1.1 GDI Generic. The same checks pass on
Linux X11/Mesa llvmpipe and macOS Apple M3 OpenGL. For Windows desktop tests, use
at least 1100 x 850 pixels and disable automatic remote-desktop size changes.

For a repeatable native modal-loop exercise, run
`WindowResizeHarness gl interactive` (Escape exits). Drag the window edges and use
the Windows system menu's Size command to hold the modal loop open. The harness
logs cached presentations and checks that the normal frame count stays unchanged
inside the event pump. This passed in the Windows VM: one sustained sizing session
presented 357 cached frames while normal frame 2351 stayed unchanged. Edge dragging
and maximize/restore also retained the image and resumed normal drawing.

Before marking the PR ready, exercise a network match, menu/tutorial/editor
dialogs, fullscreen switching, minimization, and multi-display HiDPI transitions
on supported desktops. The VM does not validate physical Windows GPU drivers.
