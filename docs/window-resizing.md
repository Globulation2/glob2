# Window resizing

## Layout foundation

Editor controls retain their distance from the right edge of the logical screen.
Their shared rectangle is updated before both drawing and hit testing, so input
received before a repaint uses the same coordinates as the next frame. Generic
map rectangles remain absolute and keep their half-open edge semantics.

Minimap positioning reads the logical screen width before drawing, hit testing,
and coordinate conversion. Physical fullscreen or HiDPI scaling does not move
controls within the logical screen.

`MapEdit::draw(frameTick)` assembles one normal editor frame. It includes dialog
timers and drawing with animation side effects, so it must only be called once
per normal frame. It is not an expose-event callback.

These changes adapt Nathan Mills (@Quipyowert2)'s editor anchoring, minimap
positioning, and frame extraction work in
[PR #64](https://github.com/Globulation2/glob2/pull/64). This foundation does not
enable window resizing or change event-loop or OpenGL-context ownership.

## Proposed live-resize implementation

Status: design for a follow-up implementation; not implemented by this PR.

### Ownership and presentation

Keep SDL window operations, OpenGL context ownership, input dispatch, simulation,
and normal rendering on the main thread. During a Windows modal move/resize
loop, present the most recently completed frame through an SDL exposed-event
watcher. SDL provides this callback opportunity in its
[Windows modal resize support](https://github.com/libsdl-org/SDL/commit/509c70c6982b6927f5a8d4fb32f9319cbaf0c2ef).
Pin and validate an SDL2 version with that support before enabling live resize.

Split normal frame completion from presentation. The normal path advances cursor
and animation state and records a complete frame. A dedicated `presentLastFrame`
path only queries drawable dimensions, clears exposed borders, presents that
frame with the existing aspect-preserving scaling, and swaps/updates the window.
It must not call `nextFrame`, screen painters, timers, input handlers, simulation,
or texture reload routines.

For OpenGL, keep the last completed image in a persistent texture or render target
owned by the main context; the contents of the default back buffer after swapping
are not a cache. Prefer rendering into an offscreen target if supported, with a
validated copy-before-swap fallback for older GL configurations. For software
rendering, retain an owned logical surface and reacquire the SDL window surface
for presentation. Never free an SDL-owned window surface. Preserve and restore GL
state (including the engine's cached state) around presentation.

Register the watcher after cache initialization and remove it before cache or
window destruction. Check the event's window ID and current thread ID; callbacks
on another thread only mark presentation pending. Use a scoped reentrancy guard
that is restored on every exit. If rendering is already active, defer presentation
until that frame completes; never recursively enter rendering. Window recreation
for graphics settings invalidates the cache and watcher binding explicitly.

### Applying a size change

Separate `resizeDrawable` from `setRes`: a notification that the OS has already
resized a window must not call `SDL_SetWindowSize` or recreate its GL context.
Coalesce size notifications and apply the latest dimensions at a safe point in
the normal loop. Update the logical surface, projection, clipping, and layout
together, then draw one complete frame. Keep explicit fullscreen/backend changes
on their own lifecycle path.

During the modal interaction, scale the cached frame; once normal dispatch resumes,
reflow the layout to the new logical dimensions. This deliberately permits a brief
scaled preview rather than trying to simulate and repaint from the callback.
Window minimum size must be set through SDL. Use drawable pixels for GL viewports
and logical coordinates for controls, preserving the existing fullscreen
letterboxing and mouse conversion behavior. Handle minimization/zero drawable
sizes by deferring allocation and presentation.

Simulation pauses while the main thread is in the modal loop. The follow-up must
bound catch-up when it resumes and measure multiplayer timeout/stall behavior.
Continuous simulation during a drag is a separate requirement needing explicit
state ownership or immutable render snapshots; a presentation callback does not
provide it.

### Acceptance checks

- Windows with the supported SDL2 version: drag all edges, hold the mouse still,
  maximize/restore, and move the window between displays. No texture corruption,
  border oscillation, deadlock, or accelerated cursor/cloud animation.
- Linux and macOS: the same resizing sequence in OpenGL and software modes,
  including HiDPI transitions, minimization, and fullscreen toggling.
- Main menu, tutorial, existing/new map editor, and nested dialogs: controls and
  minimap clicks track the displayed layout, including input before repaint.
- Instrument a resize callback: no simulation ticks, dialog timers, cursor steps,
  texture uploads, or GL-context transfers; recursive exposes terminate safely.
- Quit and window recreation during pending exposes: no callback outlives its
  window/cache. Headless and dedicated-server execution create no presentation
  resources.
- Compare a seeded headless run against the base revision; presentation changes
  must not alter simulation results. Exercise a network match with a prolonged
  resize to establish the supported stall behavior before shipping.
