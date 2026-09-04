# Live toroidal world experiment

Branch: `codex/toroidal-world`, based on committed `master` (`14b99bc8`).
No uncommitted changes from the original checkout are included.

Build and launch from this worktree:

```sh
scons release=1 -j4 build/src/glob2
./tools/run_torus.sh
```

Start a custom game (or load a replay). Click **3D world [F8]** in the upper
left. The camera pulls back to the complete map, rolls it into a tube while
joining the ends in one overlapping motion. Click **2D map [F8]** to reverse the 1.8-second transition.
Toggling during an animation reverses it from its current position. The
transition stays anchored on the center of your current map view, with one
gentle pullback instead of a separate zoom to the full flat map. The anchor's
horizontal and vertical tangent directions stay aligned with screen right/down
throughout the reveal. Starting a fresh reveal resets the zoom level while retaining the current map position.

- Arrow keys, Ctrl+arrows, minimap, or left/middle drag: move the shared map position.
- Mouse wheel: zoom; keep scrolling in to return to 2D at the new position.
- Right-click: reset zoom, keeping the current map position.
- F8: keyboard alternative to the view button.

The 3D surface contains the live terrain, resources, buildings and units from
`Game::drawMap`. Fog follows the normal visibility rules. Unexplored areas use a lit slate-blue
material with a subtle grid, keeping the shape readable without revealing
hidden terrain or units. Replay visibility controls
can show the whole map. The torus is an inspection view: use 2D for placing
buildings and painting areas. The game keeps running in either view. The center marker identifies the patch
that will fill the 2D view when you return. The map has a fixed placement on the
ring: navigation moves a surface-orbit focus, followed by the camera. Dragging
uses the projected local surface scale for consistent sensitivity across the
inner and outer walls. The camera stays 18 world units from the selected point with a fixed lens.
It tilts up to 60 degrees over the inner wall instead of entering the hole.
The rim can naturally occlude the selected patch; navigation does not force a
close-up or widen the projection to keep that patch visible. Returning to 2D unfolds around that new position.

The launcher stores all preferences, saves and replays in
`experiment/profile`, avoiding the regular `~/.glob2` directory. The build,
launcher, and runtime files are local to this worktree.

## Implementation and limits

`TorusGeometry.h` rolls a parameterized sheet with two overlapping bends. The final surface
keeps both map axes periodic, with major radius 3 and tube radius 1. The world
uses uniform texture coordinates. The earlier conformal/aspect-ratio correction
has been removed, so the inner rim no longer compresses the artwork through
that remapping. Curvature and perspective naturally affect apparent proportions.
`TorusView` captures the world into an OpenGL framebuffer at native map resolution (32 pixels per tile),
capped at 8192 pixels per axis and the GPU limit, at up to
10 Hz and renders a 160×160 mesh. Camera animation renders independently of
the atlas refresh rate. A deterministic spherical starfield with a faint galactic band rotates with
the camera behind the world. It is an artistic sky, not a star catalog.
The existing 2D HUD is drawn afterward.

This prototype requires OpenGL and a framebuffer-capable compatibility
context. Sprite artwork lies on the surface; buildings are not extruded 3D
models. The normal animated cloud and shadow layers are included in 3D when high-quality
graphics are enabled, at the atlas refresh rate. A map-discovery mask keeps
clouds off unrevealed terrain. Cloud magnification uses world coordinates on a
shared sample grid; animation uses elapsed time rather than render counts, so
switching views preserves cloud positions. Viewport particles and ghosts
are omitted in 3D. Large worlds
lose detail in the bounded atlas and cost more to capture. The regular 2D
view retains its normal rendering and controls.

The branch also contains the build/runtime compatibility fixes needed to run
the committed source on Apple Silicon with Homebrew and current SCons:
C++14 flags, Homebrew discovery, native Apple OpenGL headers, and an SDL
OpenGL window that does not create a competing software window surface.

Geometry regression check:

```sh
c++ -std=c++14 -O2 test/TorusGeometryTest.cpp -o /tmp/glob2-torus-test
/tmp/glob2-torus-test
```

The test checks exact flat endpoints, both closed seams, the torus equation,
finite continuous positions across the animation, sub-tile viewport alignment,
uniform ring geometry, fixed screen orientation throughout the reveal, and
shared navigation/2D return positions across wrapped map boundaries.

## Performance checks

The cloud renderer now submits at most 4096 patches per batch, preserving the
original four triangles and integer-rounded opacity at each patch center.
The torus uses an indexed GPU mesh, cached until latitude, zoom, viewport size,
or unfolding amount changes. Longitude navigation updates a texture-coordinate
uniform because the ring is rotationally symmetric; geometry and map coordinates
still describe the same fixed world surface.

On the Apple M3, a deterministic cloud rendering benchmark measured these median
times (five measured runs after warmup, including `glFinish`, 512×512 output):

| Cloud grid | Original | Batched |
| --- | ---: | ---: |
| 128×128 patches | 26.66 ms | 2.46 ms |
| 256×256 patches | 85.90 ms | 13.58 ms |
| 512×512 patches | 247.05 ms | 22.37 ms |

Framebuffer comparisons differed by at most 1/255 per channel. They cover odd
cell sizes, negative offsets, opacity interpolation, and multiple grid sizes.
Separate eight-second live game samples put cloud drawing at 65% before batching
and 7% afterward. Those live runs use different generated maps and are bottleneck
checks, not a controlled whole-game FPS comparison. High-resolution atlas
capture, simulation, and presentation still contribute to frame cost.

Run the cloud regression/benchmark on macOS:

```sh
c++ -std=c++14 -O3 -Wno-deprecated-declarations test/AlphaMapRenderBenchmark.cpp \
  -I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib -lSDL2 -framework OpenGL \
  -o /tmp/glob2-cloud-benchmark
/tmp/glob2-cloud-benchmark
```

The geometry regression also verifies that the hovering camera is equivalent to
transforming a fixed torus into the selected point's tangent frame, and that
unfolding preserves that point and its orientation across both world seams.

Cloud coordinate regression:

```sh
c++ -std=c++14 -O2 test/CloudFieldTest.cpp src/SimplexNoise.cpp -o /tmp/glob2-cloud-field-test
/tmp/glob2-cloud-field-test
```
