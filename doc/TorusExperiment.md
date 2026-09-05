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

Start a custom game, or load a replay. Press **G**.
The shortcut appears as **Toggle 2D / torus view** in the standard keyboard
settings. Existing customized layouts retain their bindings; add the action in
the configurator or restore defaults to get G. Holding the shortcut does not repeatedly reverse the view.

| Control | Action |
| --- | --- |
| G | Switch views; reverse an animation already in progress |
| Arrow keys / Ctrl+arrows / minimap | Move the shared map focus |
| Middle-drag in 3D | Move the camera over the torus surface |
| Mouse wheel in 3D | Zoom; continue zooming in to return to 2D |
| Left-click / left-drag | Select, place buildings and flags, or paint areas |
| Right-click | Normal game cancellation / deselection |

The 1.8-second transition uses overlapping bends and a restrained pullback.
The focus's local horizontal and vertical directions stay aligned with the
screen. Navigation moves the camera around a fixed world, including its inner
side, without an automatic dive into the central hole. The center marker shows
the map position that will fill the 2D view.

## Rendering and interaction

`TorusView` captures the normal renderer's terrain, resources, buildings, units,
clouds and shadows into a repeating texture. Unknown terrain becomes a lit
slate-blue grid, preserving the ring's silhouette without exposing the map.
Clouds in unknown areas fade with the transition. The sky and surface extend
beneath the translucent sidebar. Stars and the faint Milky Way are artistic,
not an astronomical catalog.

Picking uses the same triangles and homogeneous coordinates as the GPU. It
selects the nearest visible surface, interpolates UVs with perspective
correction, then converts them to wrapped world pixels. Native tools receive
those coordinates, so their placement validation, modifier gestures and order
creation remain in the existing game logic. Building previews and pending
buildings are captured onto the surface. Empty sky is not a map target;
releasing there cancels building placement and finishes any area painting.
Middle-drag is reserved for camera movement.

The torus has uniform angular texture coordinates, major radius 3 and minor
radius 1. A flat toroidal map cannot be embedded in an ordinary ring torus
without stretching: the inner circumference compresses and the outer one
expands. There is deliberately no conformal UV correction.

Keyboard steps and wheel zoom use a shared exponential camera response (about
63 ms time constant). Mesh projection, picking, and the distant sky use the same
rendered focus; wrapped coordinates interpolate across the shortest seam.

GPU setup and discovery uploads are separate from frame drawing. Loading a game
resets the camera, picking cache, textures and buffers together. GPU objects are
recreated when a resolution/fullscreen change replaces the OpenGL context. The
shared renderer explicitly releases old contexts, and a generation counter
prevents stale object reuse if a driver recycles a context address. The view requires
OpenGL 2 shaders and framebuffer objects; framebuffer or shader creation failure
returns to the normal 2D renderer on the same frame.

## Performance

The atlas refreshes on every rendered game frame; camera animation uses cached
indexed GPU geometry at the same cadence. Discovery uploads occur only when the
visibility mask changes; CPU cloud and discovery buffers are reused. Its resolution is 32 pixels per tile, capped at
8192 pixels per dimension and the GPU's texture/viewport limits. Larger maps
therefore downsample. Small distant tiles also lose detail through projection.
Picking caches stationary pointer hits and rejects triangles outside the
pointer's bounding box.

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
