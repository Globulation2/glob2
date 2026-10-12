# Window resizing

## On this page

- [Event and rendering ownership](#event-and-rendering-ownership)
- [Layout](#layout)
- [Timing and validation](#timing-and-validation)
- [Periodic drawing and display scale](#periodic-drawing-and-display-scale)
- [Independent graphics effects](#independent-graphics-effects)

## Event and rendering ownership

SDL window operations, the GL context, simulation, and rendering stay on the main
thread. All GUI event loops use `GraphicContext::pollEvent`. Only while that method
is inside SDL's event pump may an exposed-event watcher present a cached frame.
The watcher checks its thread and window and rejects recursive presentation. It
never enters screen painting, timers, animation updates, or simulation.

SDL3's Windows modal resize support
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
The desktop software path owns a native-pixel drawing surface and caches a completed copy;
logical primitives, sprites and glyphs rasterize directly into this surface using
the existing software backend when output scaling is needed. Normal frames are
presented without enlarging a completed logical framebuffer. The backend
reacquires SDL's window surface for each presentation. Neither backend depends
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

Run `python3 test/run_tests.py --filter 'WindowResize/*' --filter 'FullscreenAspect/*'`;
each suite has a software and an OpenGL case in `glob2-engine-tests`. The resize
suite checks cached pixels after incomplete drawing,
callback guards, normal-frame counts, GL state restoration, grow/shrink reflow,
context identity, input coordinates, minimum dimensions, and cache invalidation
on window recreation. OpenGL pixels are captured at the swap boundary rather
than reading the post-swap front buffer, which is unreliable under Mesa/Xvfb.
Linux CI runs both backends under Xvfb/Mesa.

For a Windows native modal-loop check, hold the system-menu Size interaction open,
then verify cached presentation, unchanged normal-frame counters and recovery on
release. Enable full-window dragging in both OS and remote-desktop settings.
Test network peers resuming and compare shared-tick checksum sidecars.
Physical GPU drivers, Wayland and mixed-DPI multi-monitor transitions require
platform-specific review; old acceptance runs do not cover a changed renderer.

## Periodic drawing and display scale

A viewport can show several copies of a small toroidal map. Sprite visibility
alone is insufficient: every map-space annotation must use the same periodic
copies. `forEachMapCopy` enumerates translations whose primitive bounds intersect
the viewport, including negative origins and sprite/radius overhang. Path-line
endpoints retain their shortest toroidal displacement and move together.
The map camera's minimum zoom follows the current viewport and map dimensions:
on a map larger than the view, zooming out stops when one map period fills the
view in either direction. A map already smaller than the view retains its 1:1
minimum and its repeated copies.

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

Windowed/Fullscreen and F11 share a live desktop-fullscreen transition, retaining
windowed dimensions and restoring state on failure. The transition waits for SDL to
finish the fullscreen change and any restoration resize before reading dimensions
and saving display preferences. Interface scale applies live
without recreating the window or graphics context. Renderer changes require restart.

`GraphicContext` distinguishes window points, drawable pixels and logical layout.
Layout divides actual window dimensions by the effective interface scale; OpenGL
uses the complete native drawable viewport. Input and clipping compose this output
scale with existing map transforms. Minimized/zero-sized outputs retain the last
valid target; a failed allocation also leaves that target intact.

Desktop OS scale uses SDL3's window display scale divided by pixel density for
window-coordinate layout. Display/DPI events refresh metrics. UI percentages
multiply that OS scale; automatic applies multiplier 1. `GLOB2_UI_SCALE` remains
an absolute window-coordinate override. The whole view retains its minimum layout
floor, while map zoom stays separate. Mobile and browser avoid counting density twice.

Test native fullscreen, input, clipping and live scale changes in both renderers.
Focus the application for a native fullscreen Spaces play check on macOS. Validate
Windows native displays, Wayland and mixed-DPI transitions on their actual hosts.

### Permanent regression coverage

Build `scons release=1 engine-tests`, then run:

```sh
python3 test/run_tests.py --filter 'MapRenderResize/*'
```

The `MapRenderResize` suite has a headless software case and an OpenGL case tagged
`[display:1920x1200]`; the runner checks that neither rewrites the profile's
preferences. It links production rendering and settings code, checks pixels across six
map copies, crosses path seams in both directions on square and rectangular maps,
pans corner selections into view, shrinks/grows the window, checks
sidebar clipping, and exercises settings toggles, tab changes, and the live
resolution label after native window events. It also retains
the previously temporary credits-centering regression. The GL mode reads the
rendered back buffer before swap. Every pixel must agree on whether it is colored
or black, and software RGB values match exactly. GL permits a mean absolute RGB
error of at most one channel level over colored pixels, excluding black padding.
Windows llvmpipe differed by 17 levels in one of 880 marker pixels (mean error
0.0194); directly drawing the marker without map-copy enumeration reproduced the
same difference. This accommodates line antialiasing without tolerating missing
or displaced geometry. Only the test translation unit relaxes C++ access
control; no test visibility changes are compiled into production objects. Credits'
implementation is compiled directly into that translation unit instead of linking
its normal object, allowing its internal scrolling widget to be exercised.

The GL fixture requires an unscaled 1800x1100 drawable; use a sufficiently large
desktop or the Xvfb command above. A desktop that constrains the window does not
satisfy the pixel-comparison fixture.

Both modes are wired into Linux CI, and software rendering also runs in Windows
CI. The harness uses a console entry point on MinGW. Both use disposable profiles and preference
preservation checks. Negative controls substituting the previous production
selection, path-line, effect, flag, ghost, particle and marker implementations each
fail the corresponding pixel checks. Restoring the old settings visibility policy
also fails. These checks cover rendering and UI behavior; they do not replace the
separate Windows modal-resize and multiplayer acceptance tests.

## Independent graphics effects

Display settings expose artwork, clouds, cloud shadows, building particles and
Full/Simple magic separately. Interface panels can be opaque or translucent.
Advanced graphics contains path-line transparency, smooth progress indicators,
decorative victory animation and renderer selection. Clouds and shadows can be
selected independently. All effect choices apply immediately; artwork applies
when loading a game or editor. HD artwork and automatic torus view require OpenGL;
unavailable controls retain their saved choices.

Old Full/Reduced preferences initialize missing individual choices to their former
values. Explicit individual choices take precedence. The command-line `-h` and
`-l` shortcuts still select the former Full and Reduced effects together.

Text size (100%, 125%, 150%) adjusts interface text independently of
interface/map scale in both desktop and touch layouts: menus, dialogs and, on
touch, the gameplay HUD. It is saved as `textSizePercent` and shared with the
in-game options' text-size control; the former `mobileDialogTextPercent` is
ignored. See [Text size](../development/ui-framework.md#text-size).

Related: [features and content](README.md).
