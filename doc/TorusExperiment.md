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
that will fill the 2D view when you return.

The launcher stores all preferences, saves and replays in
`experiment/profile`, avoiding the regular `~/.glob2` directory. The build,
launcher, and runtime files are local to this worktree.

## Implementation and limits

`TorusGeometry.h` rolls a parameterized sheet with two overlapping bends. The final surface
keeps both map axes periodic, with major radius 3 and tube radius 1. The world
uses uniform texture coordinates. The earlier conformal/aspect-ratio correction
has been removed, so the inner rim no longer compresses the artwork through
that remapping. Curvature and perspective naturally affect apparent proportions.
`TorusView` captures the world into a 2048×2048 OpenGL framebuffer at up to
10 Hz and renders a 160×160 mesh. Camera animation renders independently of
the atlas refresh rate. A deterministic spherical starfield with a faint galactic band rotates with
the camera behind the world. It is an artistic sky, not a star catalog.
The existing 2D HUD is drawn afterward.

This prototype requires OpenGL and a framebuffer-capable compatibility
context. Sprite artwork lies on the surface; buildings are not extruded 3D
models. The normal animated cloud and shadow layers are included in 3D when high-quality
graphics are enabled, at the atlas refresh rate. Viewport particles and ghosts
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
