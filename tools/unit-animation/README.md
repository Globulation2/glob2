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
`unitmini*.png`. The repository keeps one file per frame; packaged builds pack
them into sprite sheets (see "Release asset and bundle sizes" in
`docs/development/reference.md`).

The expanded layout has 1,792 consecutive recolorable frames and 1,024 shadow
frames. Action bases in unit types and saves keep their existing values;
`unitAnimationFrame` maps them to the render-only fourfold layout. Direction 8
retains its eight-direction turning cadence. The credits retain their existing
walk-cycle duration.

## Checks

Run `python3 tools/unit-animation/test_render.py` with Pillow installed to test
scene preparation and rejection of an incomplete install. `src/unit/render/UnitAnimationTest.cpp`
is part of `glob2-unit-tests` and exhaustively checks frame ranges and
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

After installing the pack, run
`python3 test/run_tests.py --filter 'UnitHighResolutionCache/*' --filter 'RuntimePack/*'`.
These cover every unit layer's resolution mapping, all action/direction/team-color
combinations, cached versus repeated HD compositing, sharp fallback, texture
invalidation on artwork changes, and map zoom. The `HighResolutionIntegration`
suite exercises the game's camera/editor/replay integration; the `GameSpeed` suite
remains applicable.

## Experimental live colony skins

The separate developer preview draws team zero's workers, warriors and explorers as textured meshes in
an otherwise normal map. The developer override is opt-in through
`GLOB2_SKIN_PREVIEW_DIR`, with desktop OpenGL and WebGL2 rendering and classic art on unsupported
backends or asset failure. It does not change simulation state or save formats.
Completed swarms also use the preview mesh when available; construction sites
retain their existing art. The override exercises assets without online ownership;
production assignments, the web designer and checkout are described in
[the platform architecture](../../docs/multiplayer/architecture.md).

From the repository root, using Blender **3.6.23**:

```sh
blender -b --disable-autoexec --python-exit-code 1 --python tools/skins/export_units.py -- \
  --output artifacts/skins/units
python3 tools/skins/make_paint.py artifacts/skins/units/paint.png
python3 tools/skins/test_export.py artifacts/skins/units
scons release=1 skin-preview
build/linux/client/release/src/skin-preview artifacts/skins/units artifacts/skins/comparison
GLOB2_SKIN_PREVIEW_DIR="$PWD/artifacts/skins/units" build/linux/client/release/src/glob2
```

Validate the installed package with `python3 tools/skins/test_export.py
data/skins/colony-v1`. This checks retained source and mesh hashes, complete clip
coverage, shared paint coordinates across actions, animation in all headings,
unclipped geometry, normalized lighting normals, and identical designer models.
The build-system test suite runs this check too.

The modern Blender importer converts the legacy cyclic IPO key times to
fractions. At exact direction boundaries its floating-point cycle reduction can
select the previous heading. The exporter samples the first pose 0.001 original
frames after each boundary and verifies the stepped heading throughout every
clip. Other poses retain their quarter-frame times; canonical topology and UVs
remain unchanged, so published paint stays attached to the same vertices.

The comparison harness writes all seven action sheets with classic poses
above live GPU-rendered poses, at three times logical size. Generated files stay
under `artifacts/`. `make_paint.py --pattern stripes` and `--pattern spots` provide
simple alternative opaque textures; omit the option for a checkerboard with a
pink registration stripe.

`export_units.py` preserves all original source bytes. It evaluates a canonical
surface once per unit type, unwraps it once, and transfers that fixed surface through the
original named metaball centers and scales in the shared body orientation.
Individual spherical components have no meaningful rotation; ignoring their
independent bone rolls avoids twisting a welded surface between gait cycles.
The explorer's ellipsoid body and wings retain their own orientations and field
axes, so their animated rotation deforms the attached surface.
Four fixed normalized influences
per vertex preserve texture attachment and vertex identity across actions. This
approximates the changing metaball surface; review silhouettes, seams and motion
before approving an asset. Re-exported topology is versioned as an experimental
UV layout, not yet a published customer paint format.

GSK1 stores a bounded little-endian header (magic, vertex count, index count,
one static pose or 256 unit poses, logical canvas size), shared UV float pairs, uint32 triangle indices,
then camera-space position/normal float sextuples for each vertex of each pose.
UVs use a top-left image origin. Directions and phases follow `unitAnimationFrame`.
The renderer performs depth-tested mesh rasterization into a transparent GPU
atlas, then composites into the existing sprite order. A visible-scene prepass
shares identical pose/paint tiles across frames and groups geometry uploads.
A bounded least-recently-used cache invalidates paint revisions, replaced textures
and reloaded meshes, protecting visible tiles before eviction. The atlas uses
128-pixel cells in up to four 2048-pixel pages, with a shared depth attachment.
Units retain the source sprite's separate shadow layer under the live mesh; the
source action, direction and phase select both together. This preserves ground
contact without tinting the shadow with the purchased paint. The browser
recreates these resources after context loss. Native mobile support,
geometry review, shadow matching and late-game performance remain release requirements.

The native comparison tool also provides synthetic GPU benchmarks:

```sh
build/linux/client/release/src/skin-preview artifacts/skins/units artifacts/skins/repeated --benchmark
build/linux/client/release/src/skin-preview artifacts/skins/units artifacts/skins/pages --benchmark-pages
```

Both modes draw 512 units across four paint surfaces. The first uses 128 distinct
pose/paint combinations; the second uses 512 visible combinations. Additional
animation phases can populate up to four persistent atlas pages. Each compares
on-demand tile preparation against the visible-scene prepass using the same
128-pixel raster resolution, warms up five frames, and measures forty frames.
Draw-count assertions catch missing work, and final BMP captures permit pixel
comparison. Report renderer/hardware and inspect captures alongside timings;
these isolated measurements do not establish crowded-game performance.
`--validate-cache` instead checks paint edits, texture address reuse and overflow;
it saves cold/hit/repaint/eviction images for comparison. Identical cache hits
should have identical pixels; moving a pose to another atlas cell can introduce
small GPU interpolation rounding differences.

For a full-map comparison, `scons release=1 skin-game-preview` builds a diagnostic
harness. Set `SKIN_PREVIEW_SAVE` to a saved game with at least two colonies and
`SKIN_PREVIEW_CAPTURE` to a relative BMP output path; run the harness with `-g`.
It inserts a visible row of four team-zero workers and four team-one workers
near the first colony, then captures the normal Scene renderer. Toggle
`GLOB2_SKIN_PREVIEW_DIR` between runs to compare only the replaced workers.
Set `SKIN_PREVIEW_HIDDEN_CAPTURE` to another relative BMP path to capture the
same scene with the local **Show colony skins** preference disabled, then enabled
again (the latter appends `.restored.bmp`). Screenshot paths are relative to the
game's writable profile. Clouds may animate between frames; use the captures to
review appearance transitions rather than assert whole-frame pixel equality.
For live authorization checks, set `SKIN_PREVIEW_ASSIGNMENT` to a JSON file
containing `origin`, `matchId` and `colonySkins` from a test instance, and set
`SKIN_PREVIEW_CACHE` to a disposable cache directory. Use fresh assertions.
`SKIN_PREVIEW_MODERATION_CAPTURE` enables a three-stage capture prefix and adds
an inn to expose the building color. Also set `SKIN_PREVIEW_PROGRESS` to a local
JSON progress file. The harness waits up to three minutes for authorized,
removed and restored appearances, emitting each stage on stdout. Disable the
skin through the test API after the first stage and restore it after the second;
it uses the normal one-minute refresh interval. Clouds are disabled in this
mode to permit a pixel comparison of the original and restored scenes.
The diagnostic placements are never written back to the supplied save.

The comparison harness also expects `swarm.gsk`. Generate it with Blender
3.6.23 using `--python-exit-code 1 --python tools/skins/export_swarm.py --
--output artifacts/skins/units`. This simplifies a reconstructed copy of the
retained TRELLIS source and gives it a new paint layout. The swarm camera and
silhouette are provisional; the map preview draws it at the existing swarm
sprite anchor and size, preserving building overlays and visibility checks.

### Generated swarm shapes

Colony skins choose a swarm mesh (see the
[skin ownership docs](../../docs/multiplayer/architecture.md#colony-skin-ownership)).
Besides the classic TRELLIS swarm, the game ships six shapes generated by
`tools/skins/generate_swarms.py` from seeded metaball designs, the primitive the
original glob units were modelled with. To change or add a shape, edit `DESIGNS`
in the generator, then regenerate and install every shape with Blender 3.6.23:

```sh
blender --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/generate_swarms.py -- --output data/skins/colony-v1
cp data/skins/colony-v1/swarm-*.gsk platform/apps/web/public/skins/models/
```

`-t 1` is required: metaball surfacing is only byte-reproducible on one thread.
The generator writes `swarm-<id>.gsk` and records its hashes, design, coverage
and volume in `manifest.json`; `test_skin_assets.py` fails if the generator, its
helpers or a mesh change without regeneration. A new shape also needs its id in
`src/online/SwarmMeshCatalog.h`, the protocol's `SwarmMesh` and the designer's
`SWARM_SHAPES`, in the same order; the asset test checks the lists agree. Ids
are part of signed skins, so never rename or reuse one.

Every shape is scaled to cover the same 5,200 pixels of the 128px swarm sprite
(close to the classic swarm's 5,486), and designs are proportioned so their
enclosed volumes stay within a few percent of each other. Check both in the
manifest after a change. Paint is laid out by projecting the mesh along the game
camera: the swarm has one pose and is only seen and painted from that camera, so
this layout leaves no seams on the visible surface and gives every visible pixel
the same texel density. `tools/skins/swarm_metrics.py MESH.gsk... --preview DIR
--paint PNG` (NumPy; Blender's bundled Python has it) reports texel density,
visible seams and UV islands as the game camera sees a static mesh, and can
render shaded previews for review.

`export_swarm.py --turn DEGREES` turns the TRELLIS swarm about its vertical
axis before projection while keeping its paint layout; the shipped classic
swarm uses no turn.


For a crowded-scene comparison, run `skin-game-preview` with
`SKIN_PREVIEW_SAVE` pointing to a two-colony save, `GLOB2_SKIN_PREVIEW_DIR`
pointing to the exported mesh directory containing `paint.png`,
`SKIN_PREVIEW_CAPTURE=final.bmp`, and `SKIN_PREVIEW_BENCHMARK=crowd`.
The tool adds ground and flying units in a 16×16 area around the first colony,
uses two paint variants, and renders the same changing poses with classic art
and then live meshes. It reports mean and 95th-percentile frame time plus draw
calls over 40 frames after five warm-up frames by default. Set `SKIN_BENCH_FRAMES`
and `SKIN_BENCH_WARMUP` for longer matched runs. First-frame cost and
`render.skins.*` scopes are reported separately; every frame verifies unchanged
simulation checksums and matching classic/skinned states. See the
[profiling guidance](../../docs/development/reference.md) for comparison limits.
Captures are written beneath
the selected user-data directory as `crowd-classic.bmp` and `crowd-skinned.bmp`.
Clouds and interpolation are disabled to isolate this comparison. This measures
the real Scene/map/HUD drawing path with diagnostic unit placement, not an
active simulation or a representative hardware benchmark. Record the save,
backend, display size and hardware with any reported results.
