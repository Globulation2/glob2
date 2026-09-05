# A fun experiment: an explorable toroidal world

Glob2's map wraps in both directions. This experiment makes that topology
visible: the live map unfolds into a 3D torus against a star field, then returns
to the familiar 2D view at the camera's current map position.

## Try it

Build with OpenGL enabled and launch:

```sh
scons release=1 opengl=1 -j4 build/src/glob2
./tools/run_torus.sh
```

A software-only build can be checked with `scons release=1 opengl=0`; this
configuration is built and startup-tested with SDL dummy drivers in Linux CI,
including GPU mode requested by preferences or the command line. Build options are cached by SCons, so
pass `opengl=1` when switching back to the GPU build.

The launcher keeps preferences, saves and replays under `experiment/profile`.
That directory and build products are ignored by Git. `GLOB2_USER_DIR` can also
select another profile on Unix systems.

Start a custom game, or load a replay. Press **G** to switch between 2D and the torus.
The shortcut appears as **Toggle 2D / torus view** in the standard keyboard
settings. Existing customized layouts gain missing default actions only when their keys do not conflict with a custom binding or sequence. Holding the shortcut does not repeatedly reverse the view.

| Control | Action |
| --- | --- |
| G | Switch views; reverse an animation already in progress |
| Arrow keys / Ctrl+arrows / screen edges / minimap | Move within the current view |
| Middle-drag | Pan within the current view |
| Mouse wheel in torus mode | Zoom within 3D; press G to return to 2D |
| Left-click / left-drag | Select, place buildings and flags, or paint areas |
| Right-click | Normal game cancellation / deselection |

The modes are separate. Scrolling and middle-dragging never change modes,
and stopping movement never folds or unfolds the map. Only the G shortcut
starts a 1.8-second transition in either direction. Ordinary 2D frames use the
normal viewport renderer, without full-world texture captures or torus mesh
updates. In 3D, both axes slide the map around a ring whose orientation stays
fixed. The star field follows horizontal navigation only; vertical navigation
rolls the map around the tube without tilting the sky. The overview fits the
playable area and adapts its tilt to the window.

## Rendering and interaction

`TorusView.cpp` owns camera and input state. `TorusViewRender.cpp` owns the
OpenGL drawing and resources. It captures the normal renderer's terrain,
resources, buildings, units, cloud shadows and fog of war into a repeating
texture. Clouds use a separate surface above the terrain, returning to ground
level as the view flattens. Both views sample the same world cloud field;
texture coordinates include the capture origin and texel-center offset.
The sky and surface extend beneath the translucent sidebar.

The shared building renderer draws every wrapped copy intersecting its viewport,
including sprite overhang at map seams. This fixes buildings disappearing from
the full-world texture and applies to software rendering as well. There is no
separate implementation of building or unit drawing for the torus.
Units likewise use the visible wrapped cell supplied by the map traversal,
so explorers and ground units retain their position near atlas edges and
while moving across seams.

Picking uses the same triangles and homogeneous coordinates as the GPU. It
selects the nearest visible surface, interpolates UVs with perspective
correction, then converts them to wrapped world pixels. Native tools receive
those coordinates, so their placement validation, modifier gestures and order
creation remain in the existing game logic. Building previews and pending
buildings are captured onto the surface. Empty sky is not a map target;
releasing there cancels building placement and finishes any area painting.
Middle-drag follows the ordinary map panning path. An explicit view toggle
finishes any active painting gesture before changing the projection.

The torus has uniform angular texture coordinates, major radius 3 and minor
radius 1. A flat toroidal map cannot be embedded in an ordinary ring torus
without stretching: the inner circumference compresses and the outer one
expands. There is deliberately no conformal UV correction.

Keyboard steps and wheel zoom use a shared exponential camera response (about
63 ms time constant). Surface rendering and picking share the rendered focus;
the distant sky shares its horizontal component. Wrapped coordinates interpolate
across the shortest seam.

GPU setup is separate from frame drawing. Loading a game
resets the camera, picking cache, textures and buffers together. GPU objects are
recreated when a resolution/fullscreen change replaces the OpenGL context. The
shared renderer explicitly releases old contexts, and a generation counter
prevents stale object reuse if a driver recycles a context address. The view requires
OpenGL 2 shaders and framebuffer objects; framebuffer or shader creation failure
returns to the normal 2D renderer on the same frame.

## Performance

The atlas refreshes on every rendered game frame; camera animation uses cached
indexed GPU geometry at the same cadence. Fog of war is captured by the shared
map renderer; CPU cloud pixels and mesh buffers are reused. Its resolution is 32 pixels per tile, capped at
4096 pixels per dimension and the GPU's texture/viewport limits. Larger maps
therefore downsample. Small distant tiles also lose detail through projection.
Picking caches stationary pointer hits and rejects triangles outside the
pointer's bounding box.

The overview samples clouds and shadows on a world-anchored lattice of at most
128 cells per axis. The normal 2D viewport retains its configured sampling
quality. The atlas capture computes only shadows; the elevated cloud layer is
sampled once, rather than also computing an unused flat cloud layer.
Resource sprites opt into a padded atlas despite their differing frame sizes.
This preserves draw order and allows the shared renderer to submit them in a
batch. Queuing a batched sprite does not change GPU state; submission does.
The software path continues to draw the original surfaces.

### Large-map profiling

A loaded-game benchmark is available with
`scons release=1 opengl=1 torus-render-benchmark`. Run from the repository root:

```sh
GLOB2_USER_DIR="$PWD/experiment/test-profile" SDL_AUDIODRIVER=dummy \
  GLOB2_BENCH_MAP=maps/Oazis.map GLOB2_BENCH_FRAMES=100 \
  build/src/torus-render-benchmark -g -F -m -s 1280x800
```

The benchmark compares 2D and the torus with and without clouds on the same
loaded map, with full visibility and diagonal panning. It measures rendering
only, with GPU completion included, and reports median and 95th-percentile
frame times after warmup. It excludes simulation, HUD drawing and frame pacing;
these numbers are not whole-game FPS. `GLOB2_BENCH_MODE="Torus clouds"` isolates
that case for a sampling profiler. `GLOB2_BENCH_CAPTURE` optionally saves a PPM
of the final overview. The benchmark uses a hidden window and generates no
input events.

On the Apple M3, Oazis (256 × 256) took about 130 ms per torus frame before these
changes. CPU sampling identified whole-world cloud noise generation first,
then individual resource draws and redundant state changes in sprite batching.
Optimized runs measured 12–16 ms with clouds, depending on concurrent desktop
load; ordinary 2D remained around 2 ms. The overview's color texture falls from
256 MiB to 64 MiB, and cloud-noise evaluations fall from about 3.15 million to
132,100 per frame with default settings. Geometry and the live map still update
on every rendered frame. The larger map receives 16 texture pixels per tile;
normal 2D retains its native detail.

The GPU integration check compares all resource frames before and after atlas
creation at three scales and two opacity levels, including frame dimensions.
On macOS, the maximum pixel difference was zero.

### Earlier cloud submission benchmark

Clouds now sample a periodic world-coordinate field driven by elapsed time,
so capture cadence and viewport changes do not change their position or speed.
The existing cloud triangles are submitted in batches instead of thousands of
individual GL calls. On an Apple M3, the standalone benchmark measured:

| Cloud grid | Previous submission | Batched submission |
| --- | ---: | ---: |
| 128 × 128 | 26.66 ms | 2.46 ms |
| 256 × 256 | 85.90 ms | 13.58 ms |
| 512 × 512 | 247.05 ms | 22.37 ms |

These are cloud-rendering measurements, not whole-game FPS. Maximum pixel
channel difference was 1/255. Live samples also identified cloud submission as
the original dominant hotspot; changing maps makes those samples unsuitable
for a controlled frame-rate comparison.

## Validation and remaining limits

Standalone CPU checks (also included in Linux and Windows CI):

```sh
c++ -std=c++14 -O2 test/TorusGeometryTest.cpp -o /tmp/torus-geometry
/tmp/torus-geometry
c++ -std=c++14 -O2 test/TorusPickingTest.cpp -o /tmp/torus-picking
/tmp/torus-picking
c++ -std=c++14 -O2 test/CloudFieldTest.cpp src/SimplexNoise.cpp -o /tmp/cloud-field
/tmp/cloud-field
c++ -std=c++14 -O2 test/MapRenderGeometryTest.cpp -o /tmp/map-render-geometry
/tmp/map-render-geometry
```

Checks cover flat endpoints, periodic seams, bounded camera distance, shared
navigation, projection/picking round trips during unfolding, nearest-surface
occlusion, empty-sky misses, world-pixel wrapping, and cloud sampling invariance.
`test/AlphaMapRenderBenchmark.cpp` compares legacy and batched GL cloud paths;
it needs SDL2, OpenGL and a display.

The experiment is implemented with compatibility OpenGL, and has been built on
macOS, Linux (Ubuntu 22.04 and 24.04), and Windows (MinGW-w64).
Native rendering has been exercised on macOS; other platforms still need visual
and gameplay testing. Buildings and units are
sprites on the surface, unit selection
uses the picked map cell rather than individual sprite pixels. Very large maps
still have substantial atlas cost, so matching the normal update cadence does
not guarantee identical measured FPS on every map and GPU. Software rendering retains the normal 2D
view. No torus camera state is serialized or sent over the network.

### Rendering regression

Build the optional integration executable with
`scons release=1 opengl=1 torus-render-test`. From the repository root, run:

```sh
mkdir -p experiment/test-profile
GLOB2_USER_DIR="$PWD/experiment/test-profile" SDL_AUDIODRIVER=dummy \
  build/src/torus-render-test -g -F -m -s 1120x720
GLOB2_USER_DIR="$PWD/experiment/test-profile" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
  build/src/torus-render-test -G -F -m -s 1120x720
```

The first needs a display with compatibility OpenGL; Linux can use Xvfb. The
second renders a loaded game through software and checks that torus inputs stay
inactive. The same executable can be built with `opengl=0`, including a run with
`-g` requested to exercise the software fallback. The GPU test covers cloud
transitions, both navigation axes, picking, return to 2D, and repeated teardown
and recreation. No desktop input is generated.

The fixed-axis navigation and elevated cloud layer were adapted from Giszmo's
`feat/torus-pan` branch (through `b838f8de`), whose implementation was authored by
Bob. This PR keeps its master baseline; it does not merge the separate AI trainer
and fullscreen-scaling branches. Native testing on Giszmo's display setup is
still needed to determine whether his separate startup/display issue remains.

### Navigation experiment

The movement-triggered folding from `feat/torus-pan` was removed after playtesting:
it interrupted ordinary scrolling. The fixed-axis 3D geometry, wrapped-building
fix, and shared cloud rendering remain. The experiment keeps 2D gameplay
and the optional torus view separate.
