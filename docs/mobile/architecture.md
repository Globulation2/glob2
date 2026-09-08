# Desktop, browser, Android, and iOS architecture

The ports compile the same C++ game, simulation, AI, save/replay formats, and
incremental screen API. Platform differences live at the application boundary:
build/toolchain selection, packaging, drawing, input, lifecycle, and filesystem roots.
Mobile is an experimental client target; it does not replace the desktop client.

## Build ownership and isolation

[SCons source manifests](../../scons/sources.py) own shared C++ source selection.
[Build identities](../../scons/build_layout.py) separate target, architecture,
configuration, SDK minimum, and device/simulator outputs. Compiler and dependency
fingerprints separate object caches; incompatible identity reuse is rejected and
writers to one output directory are locked.

| Target | Shared native output | Packaging / host |
| --- | --- | --- |
| Desktop | `build/<host>/client/<mode>/src/glob2` | Existing SCons executable and native SDL host |
| Browser | `build/emscripten/client/<mode>/` | Existing Emscripten build and browser host |
| Android | `build/android/device/<abi>/26/client/<mode>/lib/libmain.so` | Generated Gradle project, SDLActivity, package assets |
| iOS | `build/ios/<device-or-simulator>/arm64/15.0/client/<mode>/lib/libglob2.a` | Generated Xcode project, SDL entry point, bundle resources |

Android uses `environment=device` for both hardware and emulator binaries; its
ABI selects ARM64, ARMv7, or x86-64. iOS ARM64 device and simulator libraries are
separate targets even though their CPU architecture matches.

[Android packaging](../../mobile/android.py) stages shared libraries, the pinned
SDL Java sources, and assets. Gradle invokes SCons before packaging.
[iOS packaging](../../mobile/ios.py) generates an Xcode project through CMake;
Xcode invokes SCons for the core and compiles the application entry point.
Neither IDE project owns an independent game source list. Edit the checked-in
`mobile/android` templates or generator, not generated build directories.

Mobile dependencies build for the destination from a pinned vcpkg baseline.
They do not probe Homebrew libraries. Lock files in `mobile/` select toolchains,
dependency inputs, and bootstrap archives. Voice recording and IRC integration
are excluded from mobile builds. Compilation databases live per target.
Tooling maturity and untested IDE workflows are listed in [status](status.md).

## Shared application and lifecycle

[ApplicationHost](../../libgag/include/ApplicationHost.h) owns scheduling and host
services. [ScreenStack](../../libgag/src/ScreenStack.cpp) drives incremental screens.
Native hosts poll SDL; the browser schedules frames through its existing host.
Background/foreground and resize events are handled at frame boundaries, before
shared screen updates. Platform callbacks do not directly change the simulation.

Backgrounding suspends incremental updates, loading, rendering, and audio.
Foreground timing excludes elapsed background time. Focus loss, rotation, and
child-screen transitions cancel held input. Graphics resets and memory pressure
invalidate expendable GPU resources for recreation on the next foreground draw.
Browser visibility uses this same event path.

Some legacy nested modal loops still use the transitional wait API. Asyncify
removal, full audio interruption handling, process recovery, and coordinated
multiplayer suspension are unfinished. Retained-activity resume is not recovery
from a killed process.

## Rendering and input

`GraphicContext` retains CPU surfaces for asset loading, editing, and readback.
The portable SDL geometry backend owns GPU textures, clipping, presentation,
ordered geometry, and reset handling. Mobile selects this renderer; desktop keeps
its existing defaults and can opt in with `GLOB2_RENDERER=sdl`. The browser retains
its software default and the browser branch’s opt-in WebGL2 renderer. See the
[browser renderer design](../browser/adr-006-webgl2-rendering.md) for context recovery.

Mobile canvases expand to the window aspect ratio instead of letterboxing or
stretching art. Legacy logical minimum sizes remain, while responsive entry menus
use device UI points. The gameplay world gains visible tiles in tall windows.
Android applies system-bar, cutout, and keyboard insets to the SDL container.
This makes the viewport fill available space, but does not itself make every old
panel usable on a small phone. iOS safe-area and keyboard integration still needs work.

Native main and editor entry menus reflow, wrap translated labels, and scroll.
Gameplay touch supports tap selection, one-finger camera dragging, and two-finger
navigation during placement/painting. Building and flag placement use a preview
with explicit 48-point Confirm/Cancel controls. Confirmation uses existing tile
validation and order emission. Area strokes reuse existing editing semantics.
The eight-point movement threshold and pointer ownership prevent accidental taps;
synthetic touch mouse events are suppressed. Mouse and keyboard controls remain.

Pinch zoom is deferred. Whole-tile panning remains available. Complete editor touch
controls, phone panel reflow, tutorial adaptation, and browser touch integration
are future work; no mobile-specific simulation rules are introduced.

## Assets, persistence, and networking

[MobilePaths](../../mobile/MobilePaths.cpp) separates the asset root from writable
application storage. Android reads assets through SDL's package-aware stream API,
extracting to a versioned private directory with a completion marker. iOS uses
bundled resources. Native writable data comes from SDL's application preference
path. Desktop path defaults and browser virtual storage remain available.
Android extraction is synchronous; bounded loading, cache retirement, transactional
native saves, rotating recovery generations, and document import/export are pending.
The existing save/map/replay formats are unchanged.

Mobile currently compiles the shared native transport and its OpenSSL dependencies.
It does not yet use URLSession or OkHttp system WebSockets. The browser transport
and gateway remain intact. The proposed compatibility handshake and coordinated
120-second reconnect recovery require further shared work and mixed-platform tests;
this foundation must not be represented as qualified mobile multiplayer.

## Extending the ports

Keep simulation changes shared and deterministic. Add platform behavior at host,
renderer, input, storage, or transport boundaries. Use the existing source manifests
and packaging generators, preserve desktop/browser defaults, and test both hosts
when changing shared screens. Platform preferences and future recovery metadata
must stay outside existing save formats. Developer signing and distribution are
separate from store submission.

For commands, see the [developer guide](development.md). For tested results and
next gates, see [verification status](status.md).

## Shared resizing foundation from PR #198

The mobile branch selectively incorporates the layout work from
[PR #198](https://github.com/Globulation2/glob2/pull/198), source revision
`befa80298c4733a73151e5fc9b368f651810d4f2`. This is not a merge of that entire
branch. The shared implementation lives in `GUIBase` and the map editor:

- `Screen::updateLayout()` refreshes geometry before painting and event dispatch.
  Embedded overlays clamp their origin before translating input, even when input
  arrives before the next paint. Host resize notifications still recenter dialogs.
  Oversized dialogs start at a nonnegative origin; this does not make all of their
  contents fit a phone. Their contents still need responsive layouts.
- `RightAnchoredWidgetRectangle` retains editor controls' distance from the right
  edge. Both host resize notifications and draw/hit-test paths synchronize against
  the current logical width. The update is idempotent so these paths cannot apply
  the same resize twice. Existing bottom anchors and camera-center preservation
  remain in `MapEdit::viewportResized`.
- Legacy modal backdrops scale to the current parent surface. Incremental screen
  execution remains owned by `ApplicationHost` / `ScreenStack`.

The mobile/browser branch already updates minimap positioning through
`Minimap::resizeViewport` before subsequent input; the equivalent #198 global
window-width lookup is not added. Its desktop cached-frame presentation, SDL
polling wrapper, GL context changes, and legacy loop refactor are not imported.
Those need separate backend-aware integration if adopted, especially for Windows
interactive resizing. No additional renderer or event loop is introduced here.

The editor anchor regression cases include 320×568 and 360×640 orientations and
tablet widths. Screen regressions cover layout-before-input, embedded dialog
clamping, rotation centering, and oversized origins. These geometry tests do not
establish complete phone usability, safe-area handling, or readable typography.
Gameplay still has an 800×600 minimum logical canvas; the next UI step is to
separate readable control sizing from that legacy minimum and reflow its panels.
Pinch zoom remains deferred.

## In-game phone HUD

Native SDL-renderer gameplay now uses a phone HUD when touch is active and the
shorter window dimension is below 600 points. iOS/Android start with touch active;
a physical mouse restores the existing desktop controls. Replay UI remains on
its existing path. System and main menus are intentionally outside this change.

The world retains its existing simulation/picking coordinates and minimum logical
canvas, while HUD geometry uses `logicalUnitsPerPoint()` to remain readable on a
phone. An SDL geometry transform scales UI drawing and clipping together; it is
reset before returning to world rendering. There is no frame readback or extra
full-screen render target for this scaling. Existing art is retained.

The status strip shows free/total workers, explorers, and warriors. A 48-point
bottom action bar opens construction, flags, selection/statistics, objectives,
alliances, and the game menu. A selected building or unit opens the inspector;
choosing a placement or paint tool closes it. Portrait uses a bottom sheet,
landscape a side sheet. Dragging the sheet scrolls it, and a scrollbar indicates
additional content. Panel drawing and inverse hit-testing use the same scale and
origin, including after scrolling and rotation. Actions remain the shared game
handlers. Confirm/Cancel retain their dedicated placement strip.

Tutorial text wraps at phone width, scrolls within a bounded card, and exposes an
acknowledgment button when the script requests Space. Touch acknowledgment calls
the existing key action. UIKit safe-area insets keep the iOS HUD away from the
notch/status area and home indicator; the world still fills the window beneath
those margins. Android cutout-specific inset reporting remains to be added.

This is a first usability pass, not completion of the phone interface. The
inspector still scales and scrolls the legacy layout: small allocation arrows
need larger dedicated controls, and bottom-anchored controls can leave excess
scrolling space. In-game dialogs, replay controls, chat, some overlays/tutorial
highlight positions, adjustable UI scale, and tablet layouts need further work.
Browser rendering preserves its existing interface; the new HUD currently
requires the native SDL renderer. Pinch zoom remains deferred.
