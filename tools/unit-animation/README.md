# Core unit animation sprites

The game uses 32 poses for each of eight directions in seven animation sets.
It still advances simulation and rendering on its existing schedule (25 Hz at
normal speed). This asset change does not increase the display refresh rate.

`render.py` records the seven source files, legacy action bases, native output
sizes, and presence of a separate shadow pass. The order is explorer flight,
worker walk/swim/harvest, then warrior walk/swim/fight. Building shares harvest.

## Renderer

Use the official Blender **2.34 Linux i386 static** release. Blender 2.79 changes
the orthographic projection and surface rendering; even corrected camera
projection does not reproduce the warrior's surface detail. Blender 2.34 uses
the source rigs, materials, lights, gamma/alpha encoding, and shadow passes
directly. No animation channels are removed or modified. The explorer camera
is shifted half a pixel left/down in image space to match the shipped sprites;
all other cameras are unchanged.

Download: [Blender 2.34 archive](https://download.blender.org/release/Blender2.34/blender-2.34-linux-glibc2.2.5-i386-static.tar.gz)

On Apple Silicon, run this 32-bit Linux executable in a Linux container with
`qemu-i386`. It requires i386 libc, X11/Xmu/Xi/Xext, OpenGL/GLU, SDL 1.2, and the
legacy `libstdc++-libc6.2-2.so.3`. The latter is available in Debian's archived
`libstdc++2.10-glibc2.2_2.95.4-27_i386.deb`. Extract its libraries into a private
directory and set `LD_LIBRARY_PATH` for the renderer; do not replace system libs.
The renderer does not require a display or a working Python 2 installation.
Its missing-Python-library startup messages are harmless for `-b ... -a`.

Verified download SHA-256 values:

- Blender archive: `2caa14fc37aa272b26bb3e88b925d61a4de861f37e1820723f833ebf0e1bf125`
- Legacy C++ package: `236ed073aa04d4d704d1664eb4bfc2d32f0c47c30f6c0fc5c7d62a4a03fa7317`

## Generate

Use a temporary output directory, mounted at `/work` in the rendering container.
Run the host-side commands from the repository root:

```sh
python3 tools/unit-animation/render.py prepare \
  --output /path/to/work/scenes32 --render-root /work/render32
```

The script copies each `.blend` and changes only the scene's frame mapping,
render start/end, and output directory, plus the explorer camera alignment. Its
DNA parser verifies that no other bytes change. Original frame `t` is sampled at output frame `4*t`; the intervening
three frames evaluate the original animation at quarter-frame intervals.
Direction and shadow-pass changes remain on the original timeline. The sources
use constant (stepped) interpolation for the direction rotation and pass
switches, and cyclic gait curves. Each direction takes samples 0 through 31
within its own eight-frame interval; the next direction starts at sample 32.
The final quarter-frame therefore evaluates the current gait loop without
blending its rotation or materials toward the next direction/pass.

For each prepared scene, run inside the container:

```sh
LD_LIBRARY_PATH=/opt/legacy/usr/lib qemu-i386 /opt/blender/blender \
  -b /work/scenes32/worker-walk.blend -a
```

Repeat for the other six names from `scenes32/manifest.json`. Rendered recolorable
frames run from 0004 through 0259; where present, shadow frames run from 0260
through 0515. Render jobs may run independently. A source-density baseline can
also be prepared with `--samples 1`.

With Pillow and NumPy installed in a host Python environment, collect into a
**staging directory**, using a checkout/copy of the original 448-frame asset set
as the reference:

```sh
python3 tools/unit-animation/render.py collect \
  --rendered /path/to/work/render32 --output /path/to/work/staged \
  --reference /path/to/original/data/gfx
```

This validates every input image's size and RGBA mode, preserves its PNG bytes,
and writes a per-frame comparison report for every fourth new pose. Review that
report and composited direction/phase sheets before installation:

```sh
python3 tools/unit-animation/render.py install --staged /path/to/work/staged
```

The installer validates the complete filename set before copying and removes
obsolete numbered shadow layers. It does not install the JSON report or change
`unitmini*.png`.

The expanded layout has 1,792 consecutive recolorable frames and 1,024 shadow
frames. Action bases in unit types and saves keep their existing values;
`unitAnimationFrame` maps them to the render-only fourfold layout. Direction 8
retains its eight-direction turning cadence. The credits retain their existing
walk-cycle duration.

## Checks

Run `python3 tools/unit-animation/test_render.py` with Pillow installed to test
scene preparation and rejection of an incomplete install. `UnitAnimationTest`
is included in the C++ TestsRunner and exhaustively checks frame ranges and
turning cadence. Compare alpha separately from RGB when reviewing the generated
report: geometry/coverage matching and subtle shading differences are distinct.

## High-resolution textures

Native 32-pose sprites remain in `data/gfx`. The Blender originals live in
`datasrc/gfx/originals/units`. Approved high-resolution layers use the existing
`datasrc/gfx/production/original-derived` category and are packaged into
`data/highres/v1` alongside the other artwork. Unit icons are not changed.
The runtime keeps the native logical dimensions (32, 38 or 40 pixels) and uses a
fixed 128×128 pixel texture for every pose, regardless of native size -- 128 is
a power of two, unlike 152 or 160 (38 or 40 × 4), which the engine's texture
uploader would otherwise round up to 256 per texture, wasting most of the
allocation. This is 4× per axis for the 32px-native explorer set, ~3.37× for the
38px-native worker sets, and 3.2× for the 40px-native warrior sets -- same 32
poses and normal 25 FPS cadence throughout. Software and the classic-artwork
option use native sprites. `frames.txt`'s `scale` column is `4` for every other
frame category; unit rows carry a `0` sentinel instead, since a fractional ratio
can't round-trip through that column's integer parsing -- every consumer of a
unit frame's HD pixel size compares against the fixed 128 directly rather than
`native_size * scale`.

Prepare scenes into an external work directory mounted at the matching container
path, then run four independent Blender processes under a combined four-CPU cap:

```sh
python3 tools/unit-animation/render.py prepare \
  --output /path/to/work/scenes --render-root /work/job/rendered --highres-pixel-size 128
python3 tools/unit-animation/render-jobs.py \
  --work /path/to/work --container-work /work/job --workers 4
python3 tools/unit-animation/render.py collect-highres \
  --rendered /path/to/work/rendered --output /path/to/work/staged
```

Four workers were used on the eight-logical-CPU Mac. The runner defaults to half
of the host's logical CPUs, caps the dedicated container to that many CPU cores,
and retains per-chunk logs, completion markers and progress. Temporary prepared
Blender scenes, raw renders and comparison reports stay outside production folders.

Review the downsampled alpha comparisons and actual runtime rendering, then:

```sh
python3 tools/unit-animation/render.py install-highres --staged /path/to/work/staged
python3 tools/artwork/package_runtime.py --check
python3 tools/artwork/validate_runtime.py
python3 tools/artwork/runtime_provenance.py
```

The HD installer verifies every image and SHA-256 before copying, merges the
1,792 unit records into the existing frame/provenance manifests, and uses the
established pack capture operation to retain approved inputs under
`production/original-derived`. Other frame families, icons and all native sprites
are preserved. Later packaging needs only `tools/artwork/package_runtime.py`.

Build `unit-hd-cache-test`, then run `build/src/UnitHighResolutionCacheTest` and
its `software` mode after installing the pack. These cover every unit layer's
resolution mapping, all action/direction/team-color combinations, cached versus
repeated HD compositing, sharp fallback, texture invalidation on artwork changes,
and map zoom. `highres-integration-test` exercises the game's camera/editor/replay
integration; the existing blur and speed tests remain applicable.
