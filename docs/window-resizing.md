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
recovery was verified in the Windows LAN acceptance test below.

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
Enable "Show window contents while dragging" in Windows Performance Options as
well as full-window dragging in the RDP client. The client setting alone did not
override the guest's disabled visual effect.

For a repeatable native modal-loop exercise, run
`WindowResizeHarness gl interactive` (Escape exits). Drag the window edges and use
the Windows system menu's Size command to hold the modal loop open. The harness
logs cached presentations and checks that the normal frame count stays unchanged
inside the event pump. This passed in the Windows VM: one sustained sizing session
presented 357 cached frames while normal frame 2351 stayed unchanged. Edge dragging
and maximize/restore also retained the image and resumed normal drawing.

## Presentation benchmark and polish

`WindowResizeHarness software benchmark` and `WindowResizeHarness gl benchmark`
measure a frame containing two opaque rectangles, with test pixel readback disabled.
Each result is the median of five batches of 60 frames after a warm-up batch.
GL requests swap interval zero and waits for GPU completion; the separate
cache-only measurement includes that completion wait. These are presentation
microbenchmarks, not gameplay frame rates, and the two timings are not additive.

A release build on `pharaoh-dev-1.local`, X11/Xvfb (1280 x 1024), SDL 2.32.10,
and Mesa 26.0.8 llvmpipe produced this comparison against `02bb97db4`:

| Backend | Window | Before frame (ms) | After frame (ms) | After cache-only (ms) |
| --- | --- | ---: | ---: | ---: |
| Software | 640 x 480 | 1.986 | 1.930 | 0.081 |
| Software | 1024 x 768 | 4.962 | 4.760 | 0.211 |
| OpenGL / llvmpipe | 640 x 480 | 1.183 | 1.197 | 0.550 |
| OpenGL / llvmpipe | 1024 x 768 | 3.034 | 3.092 | 1.427 |

The software result is consistent with avoiding a redundant full-window clear
and using an unscaled blit when dimensions match. The GL difference is small
(about 1–2%); this single comparison does not establish a performance change.
The completed-frame copy remains a per-frame cost, particularly on llvmpipe.
Physical GPU and high-resolution gameplay performance still need separate profiling.

Cache resources and validity now live in one structure, explicitly released before
GL context destruction. The device texture limit is queried once per context.
Texture allocation and software allocation/copy failures invalidate the cache and
report once until recovery. The resize harness also checks letterbox pixels and
recovery after a simulated device texture-size limit. Both resize backends and all
five fullscreen/aspect sizes passed on Linux after the polish; the macOS harnesses
also compile. The Windows acceptance below describes the earlier tested version.

## Windows acceptance results (2026-09-08 UTC)

The full client from `71da3dbbc` passed the following checks in the existing
Windows Server 2022 VM, using SDL 2.32.10 and app-local Mesa 26.1.8 llvmpipe for
OpenGL. This acceptance pass required no production code changes.

- Two independent Windows clients played SmallForTwo over real loopback TCP.
  Host and guest were each held in the native system-menu sizing loop for short,
  ten-second, and greater-than-sixty-second pauses. The short/ten-second host
  tests initially used outline resizing; full-window contents were enabled
  before the sustained host test and all guest tests. The peer displayed its
  waiting message, and both clients resumed after each pause. Both accepted
  building-priority orders afterward. The guest left through the game menu and
  the host received the victory result.
- Separate `GLOB2_REPLAY_PATH` values and `GLOB2_CHECKSUM_SIDECAR=1` recorded both
  clients. All **6,371 shared ticks (0 through 6370)** matched, including total,
  team, unit, and building checksum records. The host recorded 7,276 ticks in
  total because it continued after the guest departed. There were zero shared
  tick mismatches. This establishes recovery for the tested LAN session; it
  does not simulate Internet latency, packet loss, or a public YOG server.
- The Introduction and Basics tutorial passed in software and Mesa GL: grow to
  1000 pixels wide, shrink to 640 x 480, maximize, drag the maximized title bar
  down to restore, and advance messages with Space. Tutorial text, units, and
  terrain remained intact; the GL cloud rendering also survived the sequence.
  No persistent white textures, blank regions, or duplicated sidebar edges
  appeared in the inspected frames. Animation non-advancement inside the
  callback is covered by the resize harness; this was not a frame-rate benchmark.
- The tutorial's nested Save dialog (GL) and Load dialog (software) remained
  visible and accepted Cancel after growing and shrinking with the dialog open.
  The earlier editor pass covered menu clamping and relocated minimap input.
  Both full-client tutorial backends also passed minimize/taskbar-restore.
- GL fullscreen startup and settings-button input passed. Software switched
  windowed -> fullscreen -> windowed live and retained working controls.
  GL display-setting changes retain master's existing restart requirement;
  they are not live fullscreen switches.

Physical Windows GPU drivers and mixed-DPI multi-monitor transitions remain
unverified hardware coverage, rather than known failures. The resize cache's
maximum-texture-size fallback and Windows simulation pause are intentional
limits described above. Broader campaign play and Internet multiplayer testing
can extend this coverage without representing the current acceptance checks as
an exhaustive proof of correctness.

## Repeated-map rendering audit

A viewport can show several copies of a small toroidal map. Sprite visibility
alone is insufficient: every map-space annotation must use the same periodic
copies. `forEachMapCopy` enumerates translations whose primitive bounds intersect
the viewport, including negative origins and sprite/radius overhang. Path-line
endpoints retain their shortest toroidal displacement and move together.

The follow-up audit covers these drawing paths:

| Path | Treatment |
| --- | --- |
| Terrain, resources, ground/air units, buildings, fog, area/gradient overlays | Existing viewport tile scan repeats map contents; building identity remains separate from displayed-copy identity. The optional debug-gradient lookup now wraps both indices. |
| Selected buildings in game/editor, assigned-worker circles, resource selection | Repeat selection geometry for all visible copies; clip to the map panel. |
| Unit target/debug lines | Repeat entire segments, preserving the short route through a map seam. |
| Virtual flags and their ranges/bars | Repeat the complete flag drawing pass. |
| Bullets/shadows, explosions and death animations | Repeat sprite placements, including overhang; retain existing visibility decisions. |
| Pending-building ghosts and building particles | Repeat drawing; particle age/physics and particle generation still run once. |
| Main-view map markers | Repeat the marker and preserve clipping; minimap markers and lifetime updates remain single. |
| Minimap, offscreen arrows, mouse placement previews, screen-space messages and clouds | Keep their existing view-specific behavior; these are not duplicated as map objects. |

Resolution presets now appear only when fullscreen is selected, including after
switching settings tabs. Desktop fullscreen scales the selected logical resolution,
so standard logical sizes no longer need the old “no fullscreen” marker. Windowed
mode follows the OS dimensions and ignores resolution-list selection events.

### Permanent regression coverage

Build `scons map-render-resize-test`, then run:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy python3 test/run-savegame-safety-tests.py --check-preferences build/src/MapRenderResizeHarness
SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1920x1200x24' python3 test/run-savegame-safety-tests.py --check-preferences build/src/MapRenderResizeHarness --gl
```

The harness links production rendering and settings code, checks pixels across six
map copies, crosses a path seam, pans the view, shrinks/grows the window, checks
sidebar clipping, and exercises settings toggles and tab changes. It also retains
the previously temporary credits-centering regression. The GL mode reads the
rendered back buffer before swap. Visible pixel occupancy must match exactly;
software RGB values match exactly, while GL RGB comparison allows one channel
level of antialiasing roundoff observed on llvmpipe. Only the test translation unit relaxes C++ access
control; no test visibility changes are compiled into production objects. Credits'
implementation is compiled directly into that translation unit instead of linking
its normal object, allowing its internal scrolling widget to be exercised.

Both modes are wired into Linux CI, using disposable profiles and preference
preservation checks. Negative controls substituting the previous production
selection, path-line, effect, flag, ghost, particle and marker implementations each
fail the corresponding pixel checks. Restoring the old settings visibility policy
also fails. These checks cover rendering and UI behavior; they do not replace the
separate Windows modal-resize and multiplayer acceptance tests.
