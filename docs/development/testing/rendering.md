# Rendering verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Selection lifetime regression

`GameGUISelectionHarness.cpp` links the real client objects with a test entry
point. It exercises selected building/unit deletion before the next GUI draw,
null selections, a live unit with no peer, gid reuse (the selection and the
failing-unit recording must not move to the newcomer), a building destroyed by
a tick (selection and pending shadow cleared through `ClientEvents`) and unit
conversion (the selection follows the unit). It runs headlessly, using the real
`GameGUI`, entity classes and selection setters. A friend fixture accesses the
private selection API without exposing it to game callers.

`ClientChannelsTest.cpp` covers the `src/engine/sim/` channels: team events reaching the
GUI once, in order and aged like `Team::updateEvents`; script presentation going
through `ClientCommandSink`; the SGSL Space acknowledgement in `ClientRequests`;
and order effects such as pause arriving as events. Concurrent delivery checks
FIFO completeness and coherent pulses; reentrant publication waits for the next
batch. Script-channel cases check immediate enablement queries before the client
drains commands, including ordered alias matching.

Run as the `GameGUISelection` and `ClientChannels` suites of `glob2-engine-tests`:

```sh
python3 test/run_tests.py --filter 'GameGUISelection/*'
python3 test/run_tests.py --filter 'ClientChannels/*'
```

The HUD turns those events into coalesced notification rows (`src/hud/GameEventFeed.h`).
The `GameEventFeed` suite (`src/hud/GameEventFeedTest.cpp`, unit binary) covers which
reports share a row, warp-safe attack areas, the eight-row cap, expiry once both the
game-time linger and the wall-clock floor have passed (normal speed, maximum speed,
pause), fading, and GoToEvent stepping through rows by recency, with explicit times:

```sh
python3 test/run_tests.py --filter 'GameEventFeed/*'
```

For AddressSanitizer and UndefinedBehaviorSanitizer on macOS or Linux, build the
engine tests into a separate directory with the sanitizer flags and run the same filter:

```sh
scons -j8 release=0 server=0 --build=build/tests-asan engine-tests \
  CXXFLAGS='-g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LINKFLAGS='-g -fsanitize=address,undefined'
python3 test/run_tests.py --build-dir build/tests-asan --filter 'GameGUISelection/*'
```

Use `GLOB2_SDL3_PREFIX` when building against the pinned SDL3 dependency prefix.
The native build records its library directory in the runtime search path.

SCons caches compiler/linker flags; pass `CXXFLAGS=-g LINKFLAGS=-g` to return to a
normal build. This is a direct method regression, not an interactive replay test.

## Aspect-ratio and screen-capture regression

The `FullscreenAspect` suite (`libgag/src/FullscreenAspectHarness.cpp`) opens a real SDL
window and checks native presentation pixels, clipping, logical screen captures,
and translated mouse motion/button events and polling at equal, wide, tall and odd
window sizes, accepting actual OS constraints. Desktop fullscreen follows the same
native display metrics. The software case also checks every pixel in 24 opaque/translucent
rectangle intersections, including rectangles above the clip area and empty
rectangles. It does not load a game profile or change saved display settings.

```sh
python3 test/run_tests.py --filter 'FullscreenAspect/*'
```

Both cases are tagged `[display:1600x1400]`: the runner opens that Xvfb screen on
Linux without a `DISPLAY` and uses Mesa software OpenGL (`LIBGL_ALWAYS_SOFTWARE=1`)
in CI. The OpenGL case is skipped in `opengl=0` builds. The same goes for the
`WindowResize` suite (`libgag/src/WindowResizeHarness.cpp`), which resizes the window
through the cache, callbacks, reflow, context recreation and minimum-size paths and
checks live scale/fullscreen transitions, F11, preserved window dimensions and
context identity. `TextRaster` checks glyph pixels against an independent native
font raster in both OpenGL and software, including fractional output scaling.

## Entering unit draw regression

Run `python3 test/run_tests.py --filter 'EnteringUnitDraw/*'`; the case is tagged
`[display:1024x768][artifacts]`.

A unit on its final step into a building keeps its map slot on the tile it is
leaving (`Unit::handleActionEnteringBuilding`) while `posX`/`posY` already name
the building tile, so `Game::drawMapGroundUnits` visits it one square behind
itself. Nothing in `Game::drawUnit` reads `displacement`, so at equal `delta`
that state must render pixel-for-pixel like the same step expressed as an
ordinary walk onto the destination tile. The harness renders both and compares
framebuffers over five points of one step; a mismatch is reported in pixels
against the 32 px tile size. It protects the fix in `src/unit/render/UnitDrawGeometry.h`
for issue #230, where the sprite was anchored on the stale map slot and the glob
walked backwards into the square it came from.

Frames land in the case's artifact directory (`artifacts/tests/EnteringUnitDraw/...`
or `GLOB2_TEST_ARTIFACTS`): `entering-delta<N>.png` and, on failure,
`walking-delta<N>.png` for the unit-only comparison, plus `scene-delta<N>.png` with
terrain and the inn for looking at by eye. The comparison itself stays on the
unit-only render, which has no animated water or clouds to make two frames differ
by themselves. The `FailingUnitMarkers` suite works the same way and leaves its
scene captures next to them. Both need a display and a GL context and are left
out of `opengl=0` builds.

## Pre-game map preview regression

Run `python3 test/run_tests.py --filter 'MapPreview/*'`: three headless cases (geometry,
codec, network) and one `[display][artifacts]` case that writes the native software
captures into its artifact directory. Fixtures exercise rectangular
placement, toroidal dragging, legacy/new codecs, malformed input, network frame
bounds, thumbnail request deduplication, timeout/retry and bounded cache reuse.
See [pre-game preview behavior and compatibility](../../features/pre-game-map-preview.md).

## Zone boundary rendering

Run `python3 test/run_tests.py --filter 'PortableRenderer/*'` on a windowing
display. The boundary regression checks every pixel along joined
outlines at 25%, 33%, 50%, 75%, 100% and 150% zoom, with fractional camera offsets
and toroidal copies. It covers software, SDL portable and (when compiled) OpenGL
renderers and verifies that later map artwork retains its transform.
Set `GLOB2_ZONE_EVIDENCE_DIR` to an existing ignored artifact directory to capture
the 33% outline fixtures.

Zone boundaries use `GraphicContext::drawMapBoundary`: positions snap to the
active target's pixel grid and strokes remain at least one target pixel wide,
up to an optional cap in screen points. Ordinary UI lines retain their existing
sizing behavior. With adaptive zoom detail the game fades these outlines out as
the map zooms out and fills zones with `drawMapFill` instead; see
[Adaptive zoom detail](../../architecture/zoom-detail.md#adaptive-zoom-detail).
`ZoomDetail/*` in the unit tests covers the curves that decide when.
`MapRenderResize/building sprites fade out*` reads rendered building pixels across
the fade in software, SDL portable and OpenGL backends, and checks the complementary
icon opacity. It catches an opaque building disappearing abruptly at the fade's end.

## Software renderer

The opt-in `software-render-benchmark` tool profiles loaded games through the production
software renderer. It is part of `glob2-tools`, not a CI timing threshold. See
[Software rendering architecture and profiling](../../architecture/software-rendering.md#software-rendering-architecture-and-profiling)
for fixture capture, paired CPU measurements and diagnostic overrides. The
`SoftwareRenderer` suite checks raster sampling, ordering, opacity revisions and
terrain-cache correctness; `PortableRenderer`, `WindowResize`, `MapRenderResize` and
`HighResolutionIntegration` cover the shared facade and window lifecycle.


For lobby screen automation, use [lobby automation](lobby-automation.md).

## Native main Settings redesign

Run `python3 test/run_tests.py --filter 'Settings/*'`. The `Settings` suite
(`src/ui/settings/SettingsScreenTest.cpp`) runs one case per configuration of the old matrix:
640×480, 800×600, 1000×700 and 1280×900 in OpenGL, 1000×700 in software rendering,
and 640×480 with expanded English strings written into the case's disposable
profile. Each case uses its own profile and writes its native captures to its
artifact directory. It covers all six categories, building stages, automatic
saving and retry, software display confirmation/rollback, pending OpenGL changes,
language refresh, and keyboard sequence/conflict handling. The shared dropdown
checks cover anchoring, mouse and keyboard selection, dismissal, wrapping, and
scrolling without committing a value. Persistence failures use a directory at the
destination path so both settings and shortcut retries exercise the atomic writer.
Window and drawable dimensions are logged so 1× runs are not mistaken for physical
HiDPI validation.

The redesigned screen exposes semantic row IDs (for example `gameplay.speed`)
for tests; do not locate settings controls by pixel coordinates. Slider updates
preview immediately and commit on release/idle, whereas discrete changes save
immediately. Bindings commit only after a complete edit. Legacy preference and
keyboard file formats remain unchanged.

The `GameSpeed` suite (`src/game/GameSpeedTest.cpp`) has a headless case for the speed
presets, bounds, legacy settings and persistence, a display case for the main and
in-game settings, language refresh, keyboard shortcuts, multiplayer eligibility and
camera cadence, and a display case that runs the live engine at normal and maximum
speed, through pause and hard pause, and plays the recorded replay back at 1x,
maximum and fast-forward. The last case captures the engine's per-run checksums
and requires the first four (speed and pause) and the last three (playback) to
agree. `python3 test/run_tests.py --filter 'GameSpeed/settings*'` runs the settings
case alone. That case also clicks the top bar's speed chevrons. The `GameSpeedControl`
suite (`src/hud/GameSpeedControlTest.cpp`, unit binary) covers the chevron presets and
the tick-rate readout's window, one-second refresh, stall decay and formatting with
explicit times.


## Landscape preview scheduling and scrolling

The same suite provides focused checks:

```sh
python3 test/run_tests.py --filter 'CustomGameSetup/preview queue*'
python3 test/run_tests.py --filter 'CustomGameSetup/landscape preview*'
```

The preview queue case checks deferred startup, viewport-center
priority for grid and single-column layouts, scrolling/filtering, unchanged seeds,
cooperative viewport-only polling, retries, and restart/reroll behavior. The
normal headless harness also runs these checks. `landscape-responsive` verifies
that completed off-screen images do not allocate rendering surfaces and that
scrolling never starts fades; it captures top/middle/bottom screenshots. CI runs
both the headless harness and this graphical regression.

`landscape-performance` runs the same fixed-image fixture without enforcing the new
behavior, for comparison with a baseline build. It reports delivery and scrolling
times for 67 completed 256×256 thumbnails, plus terrain hashes and colony positions
for three generators with root seed 71. Timings are evidence, not pass/fail limits.
