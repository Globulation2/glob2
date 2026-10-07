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
python3 tools/skins/make_paint.py artifacts/skins/units --material mixed
cp data/skins/colony-v1/swarm.gsk artifacts/skins/units/
python3 tools/skins/test_export.py artifacts/skins/units
python3 tools/skins/install_units.py artifacts/skins/units
scons release=1 skin-preview
build/linux/client/release/src/skin-preview artifacts/skins/units artifacts/skins/comparison
GLOB2_SKIN_PREVIEW_DIR="$PWD/artifacts/skins/units" build/linux/client/release/src/glob2
```

Validate the installed package with `python3 tools/skins/test_export.py
data/skins/colony-v1`. This checks retained source and mesh hashes, complete clip
coverage, shared paint coordinates across actions, animation in all headings,
unclipped geometry, normalized lighting normals, and identical designer models for
all seven unit actions and the swarm. Workers and warriors additionally require
one closed connected surface, separate limb regions, matching front/back and
top/bottom UVs and triangle interpolation, noncollapsed connection triangles,
and a continuous flip-cycle boundary. `test/build_system/test_skin_surface_contract.py`
injects broken paint, a cross-foot triangle and a collapsed joint to check these
regressions are rejected.
The build-system test suite runs this check too.

The modern Blender importer converts the legacy cyclic IPO key times to
fractions. At exact direction boundaries its floating-point cycle reduction can
select the previous heading. The exporter samples the first pose 0.001 original
frames after each boundary and verifies the stepped heading throughout every
clip. Other poses retain their quarter-frame times; canonical topology and UVs
remain unchanged, so published paint stays attached to the same vertices.

The comparison harness writes all seven action sheets with classic poses
above live GPU-rendered poses, at three times logical size. Add `--all-phases`
to write eight numbered pages per action covering all 32 phases in every
heading (the static swarm has one page). Run it with white, stripes, spots and
an isolated patch to review both geometry and paint attachment. Generated files stay
under `artifacts/`. `python3 tools/skins/make_paint.py <DIR> [--pattern
checker|stripes|spots|solid] [--color RRGGBB] [--material
glossy|matte|metallic|hairy|mixed]` writes the colony-v2 pair `<DIR>/paint.webp`
(512x512 colour atlas, one 256x256 quadrant per model: worker top-left, warrior
top-right, explorer bottom-left, swarm bottom-right) and `<DIR>/material.webp`
(the matching material-id map). The generator requires Pillow with WebP support
and also retains PNG source copies for image editing and analysis. `--pattern stripes`, `spots` and `solid` provide
simple alternative opaque textures; omit the option for a checkerboard with a
pink registration stripe. `--material mixed` cycles every material for shader checks.

`export_units.py` preserves all original source bytes. Worker and warrior surfaces
are constructed from the retained definition in
`datasrc/gfx/authored/skins/limb-surfaces.json` and `tools/skins/limb_surface.py`.
A spherical torso has four shared socket rims; each limb follows an explicit
path through its named source components (indices follow sorted `Mball` names).
Section rings follow their own limb path with a minimum neck radius, a smooth
transition from the exact socket rim and a spherical terminal cap. Small socket
openings retain the surrounding torso shell. Each clip aligns the body axes to
its source rig; swimming sources use a different rest orientation. Parallel
transport keeps ring orientation continuous when a fighting arm bends along the
body's front axis. Vertices then fit the shared torso/proximal-component field
and their own distal limb field, with analytic field normals to smooth connections.
Other limbs' joints and tips never influence that limb's surface. This
keeps distinct feet from acquiring a welded bridge and prevents a warrior arm
from being pulled toward another limb. Topology and vertex identity stay fixed
across all actions; source centers, scales, cameras and gait samples are retained.
These surfaces approximate the original metaballs, so silhouette and motion
review remain necessary alongside structural checks.

Workers and warriors share a virtual body chart: counterpart front/back and
top/bottom surfaces sample identical UVs, including the upper/lower limbs exchanged
by a flip. Reflection-invariant center fans avoid different interpolation on
opposite sides of a quad. Painting either the texture or the preview therefore
preserves symmetry without a symmetry switch, duplicate stamps or server-side
texture rewriting. This also applies to fill, erase and arbitrary uploaded paint.
The retained surface contracts record reflection partners and limb regions.
The explorer retains its original geometry, normal calculation and four-influence
transfer, including ellipsoid orientations; only its UVs change to a body-space
front/back and top/bottom folded chart. Swarm geometry and UVs are unchanged.

`install_units.py` validates all three generated sets before copying unit meshes,
surface contracts and provenance into `data/skins/colony-v1`, and copies all unit
actions into the web designer. Preset paint remains reproducible through
`make_paint.py`; changing UVs alone does not require replacing immutable preset
texture bytes. Colony Studio offers free camera orbit, action, animation and paused-frame
controls. Scrubbing keeps the loaded GPU model; selecting an action loads the
corresponding runtime mesh. Painting freezes the displayed pose; the swarm's
separate final-view mode changes only the standardized game camera azimuth. The mesh UV layout remains experimental `colony-v1`;
skins paint each model through its quadrant of a `colony-v2` atlas.

GSK1 stores a bounded little-endian header (magic, vertex count, index count,
one static pose or 256 unit poses, logical canvas size), shared UV float pairs, uint32 triangle indices,
then camera-space position/normal float sextuples for each vertex of each pose.
UVs use a top-left image origin. Directions and phases follow `unitAnimationFrame`.
The editor reconstructs model space using versioned, hash-bound `.view.json`
sidecars; rotating raw GSK positions would distort their independently scaled
depth. After changing any mesh export, regenerate these camera transforms and
native swarm constants with the pinned Blender executable:

```sh
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/export_views.py
python3 tools/skins/studio_thumbnails.py  # requires NumPy
```

The first command leaves existing GSK payloads untouched, writes the sidecars to
both installed/native and web model directories, and updates
`src/online/SkinViewTransforms.h`. The asset contract checks their mesh hashes,
matching copies and generated native matrices. `studio_thumbnails.py` updates the
model/action selectors under the web public assets directory. The skin projection
unit suite checks depth coverage, seam margins, curated fill masks and full-ring
bounds; `SkinMesh` native tests verify world height, normals and cache isolation
for transformed views. Shader parity checks use `skins/materialShader.ts`.

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
comparison. The helper lets the desktop window settle before captures and prints
the actual SDL video driver, OpenGL vendor, renderer and version in benchmark logs.
Report renderer/hardware and inspect captures alongside timings;
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
`--turn DEGREES` turns the model about its vertical axis before projection,
keeping its paint layout; the shipped classic swarm uses no turn.

For a crowded-scene comparison, run `skin-game-preview` with
`SKIN_PREVIEW_SAVE` pointing to a two-colony save, `GLOB2_SKIN_PREVIEW_DIR`
pointing to the exported mesh directory containing `paint.webp` and `material.webp`,
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
helpers or a mesh change without regeneration, or if the designer's copies in
`platform/apps/web/public/skins/models` differ. A new shape also needs its id in
`src/online/SwarmMeshCatalog.h` and the protocol's `SwarmMesh`, in the same
order as `DESIGNS` (the asset test checks the three agree), and a name and
description in the designer's `SWARM_SHAPES`. Ids are part of signed skins, so
never rename or reuse one; clients that do not know an id reject skins using it.

Every shape is scaled to cover the same 5,200 pixels of the 128px swarm sprite
(close to the classic swarm's 5,486), and designs are proportioned so their
enclosed volumes stay within a few percent of each other. Check both in the
manifest after a change. Paint is laid out by projecting the mesh along the game
camera: the swarm has one pose and is only seen and painted from that camera, so
this layout leaves no seams on the visible surface and gives every visible pixel
the same texel density. `tools/skins/swarm_metrics.py MESH.gsk... --preview DIR
--paint PNG` (NumPy; Blender's bundled Python has it) reports texel density,
visible seams and UV islands as the game camera sees a static mesh, and can
render shaded previews for review. To see a shape in a real scene, copy
`data/skins/colony-v1/*.gsk` beside a `paint.webp` under `artifacts/`, point
`GLOB2_SKIN_PREVIEW_DIR` there and set `GLOB2_SKIN_PREVIEW_SWARM=<id>`; the
previews above then draw team 0's swarm with that shape, painted from the
atlas's swarm quadrant. `--paint` takes a 256px swarm paint, such as that
quadrant cut out of a colony-v2 atlas.

### GSR1 rig migration (opt-in unit previews)

GSR1 is a presentation-only alternative to animated GSK1. Worker walk, warrior
walk/swim/fight, and explorer flight are opt-in candidates: `GLOB2_SKIN_RIGS=1`
selects them in gameplay,
`skin-preview`, and `--render-skin`; `VITE_SKIN_RIGS=1` selects it in Colony
Studio. Native gameplay and Studio fall back to the corresponding baked clip if its rig is
missing or invalid. Offline sprite generation fails on an invalid rig rather
than publishing a silently different recipe. `GLOB2_SKIN_DEFORMATION=cpu`
forces the native CPU deformation fallback for comparison. Software clients
continue consuming published sprite bundles.

The installed manifest's `rigs` entries record format, SHA-256, explicit clip
mapping, exporter/reference hashes, and acceptance status. `accepted: false`
means a development candidate, not permission to change the default. The
remaining worker swim and harvest clips still use GSK1. Do not remove the baked
assets or enable rigs by default until the complete catalog passes visual,
publication, performance and platform acceptance. In particular, baseline M3
and lower-power hardware measurements cannot be inferred from a Linux software
renderer. Existing sprite bundles remain immutable; the offline rig preview
uses a distinct render-recipe digest, including GSR bytes, camera metadata,
evaluator and shader inputs.

Generate the worker candidate and its editable Blender scene in ignored staging:

```sh
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/author_worker_rig.py -- --output artifacts/rig/worker
```

The authoring script creates a symmetric, rounded rest surface with welded
socket transitions and geometry-derived normals. It preserves the installed
worker's vertex order, indices and reflected paint UVs, but deliberately replaces
the old metaball-derived shape. `worker_surface.py` owns the proportions and
weights; all nonzero contributions fit four influences. A 2.3-unit capsule
midsection gives the worker its tall torso, separating the upper and lower
attachment regions without scaling the limbs. Socket weights are evaluated in
the unextended torso coordinates, and complete lobe controls translate with the
body during the gait. Short connectors balance the taller torso, with 1.7-unit
shaft radii and 2.05-unit terminal radii. A waist narrowed in depth shapes the smooth torso
while preserving both reflection symmetries. This shaping does not alter limb
geometry or attachment weights. The original
hands' forward sweep drives a smooth torso curl bounded to 0.14 radians per end:
chest and hips bow forward through the waist using their existing attachment
controls. Whole lobes rotate together, so this adds body flex without bending the
shafts or squashing terminal caps. Each lobe uses a
straight shaft with equal weights around each cross-section and a rigid terminal
cap. Its three controls share one orientation and slide axially to change reach;
the surrounding torso partially follows the attachment. Walk retargeting keeps
the terminal paths from the previous gait while bounding swing to 0.7875 radians
and enforcing 60-degree clearance between lobes. These are deformation controls for a continuous blob, not elbow or knee
joints. The broader sockets and cylindrical shafts avoid pinched, detached-looking
hands and feet, with some changes to the silhouette and terminal paths.
The source animation remains an offline motion reference only.

The generated `.blend` contains `WorkerRestSurface`, its `PublishedPaint` UV
layer, and `WorkerRig`: `body` plus arm and leg chains on both sides, each with `attach`, `upper`,
and `lower` bones (for example `arm.upper.R`). The final `.R`/`.L` suffixes
support Blender's mirrored-pose naming convention. The `Walk` action uses quaternion
channels at 32 fps, frames 1–64, with a closing key at 65. Disable the action or
select the armature's rest display to inspect the neutral shape. Rotate and translate `attach` to steer a whole lobe. The `upper` and `lower`
controls have rotation locked and move only along local Y to set reach; keep them
in radial order. Scale stays locked. Imported actions are checked for shared
orientation, axial offsets and control order, as well as valid sampled transforms. Blender's between-key quaternion interpolation is not the runtime's
exact slerp; check exported motion in the production preview.

To prototype another animation, save an edited copy under ignored `artifacts/`,
create a named quaternion action on the same bones, and author its two-second
cycle over frames 1–65 (65 repeats 1). Import it into a fresh output directory:

```sh
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/author_worker_rig.py -- --output artifacts/rig/reach \
  --action-blend artifacts/rig/edited-worker.blend --action Reach
```

This samples frames 1–64 onto the reproducible base mesh and skeleton. It exports
a **worker-walk preview**, not a new gameplay animation identity; adding another
gameplay clip still requires its frame/clip mapping and catalog integration.
The action import deliberately ignores mesh, rest-bone, constraint and object
edits; bake desired motion to quaternion bone TRS channels first. Keep edited
sources separate: the generator replaces its output `.blend` on every run and
refuses to overwrite the action source itself. Changes to the authoring scripts
require regenerating the rig and manifest provenance. The candidate remains
opt-in pending visual approval, including representative published paints.

Generate warrior and explorer candidates with their editable scenes:

```sh
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/author_unit_rigs.py -- --model warrior --output artifacts/rig/warrior
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/author_unit_rigs.py -- --model explorer --output artifacts/rig/explorer
```

The warrior has nine bones: `body` and two controls for each arm/leg on both
sides (`arm.upper.R`, `arm.lower.R`, etc.). Walk, Swim and Fight share identical
rest geometry, weights, skeleton and paint coordinates. These are deformation
controls for a continuous blob, rather than anatomical joints. The rest surface
uses a round torso and four identical spherical terminal lobes. Left/right and
top/bottom reflection symmetry is a base-mesh constraint: all four lobes use the
same reference control lengths, socket profile and mirrored weights. All four
socket openings expand together on the torso sphere, with a 2.1-unit minimum
connector radius and 2.78-unit terminal radius. Partial control influence on the
surrounding body lets each attachment region move with its lobe. The shafts have
no torso-bone influence: their two controls share an orientation and blend only
axial translation, with weights distributed along the full connector so inward
motion cannot fold the middle rings backwards. Cross-sections retain their shape and the centerline stays
straight as reach and angle change. Diagonal rest directions accommodate the rolling walk
and its exchange of upper/lower lobes. Terminal caps follow one control rigidly
to retain their volume. Animation poses can differ between lobes; their underlying
shape and deformation rules must not depend on an arm/leg label.

Motion transfer retains the legacy body rotation, translation and timing. Walk
and fight bound limb direction changes to 0.5 radians at the body and 0.65 at the
next control. Swim transfers the source lobe centers directly for its outward
stroke. An initial contact pass keeps terminal centers at least 6.3 model units
from the torso center for walk/fight, 6.0 for extended swim poses, and 5.7 apart.
A second pass separates converging shaft directions to at least 60 degrees.

Swim recovery then gathers the lobes toward a compact pose with centers 3.5 units
from the torso. Retraction follows the source's mean lobe reach, smoothly entering
below 6.5 units and reaching full tuck at 2.7. The lobe directions return toward
the symmetric rest arrangement as the shafts shorten, giving the full-sized caps
room to gather without crossing. Both controls share one orientation; neither the
body nor the terminal caps scale down. The short connections keep their transverse
profiles while the attachment regions move with them. This produces a deep inward
stroke without reproducing the original metaball unions. Quarter-frame intersection
checks cover the transitions as well as the authored samples.
Swing and contact limits apply
to motion transfer; imported actions are not clamped. The upper handle sets each
lobe's angle and placement; the distal handle changes reach along local Y and
locks independent rotation and sideways movement in Blender. Action import
rejects mismatched shaft orientations or a sideways distal offset, preventing
accidental bending. Scale stays locked in the editor. Recheck new actions for
intersections and silhouette quality.

The explorer has four bones: `body`, `head`, `wing.R` and `wing.L`, with a Fly
action. Its ellipsoid proportions live in the rest mesh rather than nonuniform
bone scales. UV-split vertices are welded for one smoothing iteration and normal
calculation, then mapped back to their published paint layout. Most of each wing
follows its bone rigidly; the blend is confined to the root. Both scenes use
quaternion actions, frames 1–65, and the same alternating gait-half frame mapping
as the worker.

To sample a new action on either base, use the same action-only import workflow;
`--clip` selects an existing gameplay slot to preview:

```sh
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/author_unit_rigs.py -- --model warrior \
  --output artifacts/rig/warrior-gesture --action-blend artifacts/rig/edited-warrior.blend \
  --action Gesture --clip fight
```

Run `tools/skins/test_unit_rigs.py` in pinned Blender for rest-surface, left/right
and top/bottom geometry/normal/weight/control symmetry, sampled intersections,
cycle closure, shared-base, rigid terminal shape, reference terminal girth and
minimum connector girth across all actions, straight centerlines and unchanged
cross-sections, independent joint posing, action round trips and invalid shaft
action rejection. The installed asset test also verifies provenance, native/web
byte identity, and unchanged paint UVs and triangle order for all five candidates.

Capture all 256 gameplay poses through the production 128px atlas, using neutral
paint under all four materials, checker paint, and a mixed-material checker:

```sh
scons release=1 server=0 skin-preview
build/linux/client/release/src/skin-preview \
  artifacts/rig/worker/worker-walk.gsr artifacts/rig/worker/review --rig-review
```

This writes six 2048×2048 BMP sheets (`review-0.bmp` through `review-5.bmp`),
16 frames per row. It also accepts a baked `.gsk`. Set
`GLOB2_SKIN_DEFORMATION=cpu` for the fallback comparison. Readback is offscreen,
so a desktop window resize does not alter the captured resolution.

Run the authored-surface checks in pinned Blender:

```sh
blender-3.6.23 --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/test_worker_rig.py
```

They cover watertight rest topology, reflected shape/weights, paint coordinates,
non-adjacent triangle intersections through the walk and independent joint bends,
loop closure, and action-export round trips. These are bounded deformation checks,
not proof that arbitrary future poses cannot self-intersect.

For production-rendered rig thumbnails, first export a neutral-paint bundle with
`GLOB2_SKIN_RIGS=1 glob2 --render-skin` using its normal manifest, texture,
material and output-directory arguments. Then extract the worker selector:

```sh
python3 tools/skins/studio_thumbnails.py --sprite-bundle artifacts/rig/sprites \
  --clip worker-walk --output artifacts/rig/thumbs
```

This path requires Pillow, validates the page hash and dimensions, and crops
frame zero from the production renderer. No separate rig parser or deformation
implementation is used. The legacy thumbnail command remains available during
the catalog migration.

#### Binary and deformation contract

All words are little-endian uint32 or IEEE float32; no native struct layout is
serialized. Maximum payload size is 16 MiB. GSR1 has no optional/trailing chunks.

| Record | Fields in order |
| --- | --- |
| Header (28 bytes) | `GSR1`, vertices, indices, bones, clips, logical canvas size, payload byte count excluding header |
| Vertex (64 bytes) | rest xyz, unit normal xyz, top-left UV, four uint32 bone indices, four float weights |
| Triangles | uint32 indices, divisible by three |
| Bone (68 bytes) | parent uint32 (`0xffffffff` for a root), local rest TRS, model-space inverse-bind TRS |
| Clip header (128 bytes) | numeric id, sample count, duration, row-major model-to-clip mat4, model-to-camera normal mat3, pivot xyz, radius |
| Clip frame mapping (2,048 bytes) | 256 `(heading radians, time seconds)` pairs |
| Clip tracks | sample-major local TRS for each bone, uniformly spaced over the duration |

A TRS is eight floats: translation xyz, quaternion xyzw, positive uniform scale.
Export rejects shear, reflections and nonuniform scale. The decoder checks
3–8,192 vertices, 3–49,152 indices, 1–32 bones, 1–8 clips, 1–256 samples per
clip, canvas size 1–128, exact payload length, finite/bounded scalars, unit
normals/quaternions, normalized nonnegative weights, every influence index
(including zero-weight slots), unique clip ids, parent ordering, inverse bind
consistency, camera invertibility/orthogonality and track/frame bounds. Scalar
magnitudes cannot exceed 10,000. Scale products across the animated hierarchy
and inverse binds stay in [0.0001, 10,000]. Loading publishes a new immutable
model only after all checks pass; failures leave an existing mesh untouched.

C++ and TypeScript validate decoded float32 values using binary64 arithmetic.
Inclusive scale/duration lower bounds and heading bounds use their serialized
float32 endpoints; norm and weight-sum tolerance is binary64 `0.0001`. Accepted
quaternions and weights normalize into float32 storage in both evaluators.
Shared fixtures test the adjacent float32 values around these boundaries, so a
language's literal rounding cannot silently change which assets it accepts.

Tracks contain absolute local transforms, not deltas from rest. Time wraps in
both directions. Translation and scale interpolate linearly; quaternion
interpolation uses shortest-path slerp, normalized linear interpolation for
absolute dot products at least 0.9995, and antipodal sign correction. A bone
palette is `heading-about-pivot * global-animated * inverse-bind`. Positions
use four-weight linear blending. Normals blend each bone's inverse-transpose
linear transform, then normalize; a cancelling vector shorter than `1e-8`
uses `(0,0,1)`. The orthographic camera transforms positions independently of
normal rotation, and normals normalize again after camera rotation. The
renderer alone applies the existing 1.25 atlas padding.

Gameplay still calls `unitAnimationFrame`. In the worker mapping, frame `f`
has heading `-floor(f/32)*pi/4` and time
`(32*(floor(f/32)%2) + f%32)/32` in a two-second, 64-sample cycle. This retains
both gait halves. Direction 8 still maps to phase zero of successive headings
through the existing function; it is not a ninth heading. Continuous evaluation
exists for tooling, but gameplay timing and discrete heading selection do not
change.

`SkinModel` owns immutable geometry, bones and `SkinClip` tracks.
`SkinMesh::fromModel(model, clip)` is a
migration adapter for all 256 frames of a clip, with no owned pose meshes or baked
pose array; each draw still selects its discrete frame.
`evaluate` reuses caller buffers. The context keeps at most 16 uploaded rest
models, while the existing four-page atlas retains its normal bounded eviction.
Consecutive paints share the same palette. Failed shader capability/compilation
uses reusable CPU uploads; failed mesh rendering retains classic-art fallback.
Destroying/restoring the renderer reconstructs GPU resources from retained
models. Studio uses the same TypeScript contract for its displayed pose and
brush projection; fill/pattern charts and inspection fit use the fixed rest
mesh.

#### Repeatable verification

`SkinModel` and `skin-rig.test.ts` consume the same analytic fixture under
`test/fixtures/skins/rig.json`; regenerate it with `tools/skins/rig_fixture.py`.
The fixture covers hierarchy, weighted deformation, normals, antipodal rotations,
frame mappings, affine camera translation with rounded weights, and malformed assets.
Run the contract and Studio projection tests
from the repository root:

```sh
python3 test/run_tests.py --binary unit --filter 'SkinModel/*'
npm exec --prefix platform -- vitest run --root platform \
  apps/web/test/skin-rig.test.ts apps/web/test/skin-projection.test.ts
```

`SkinModelRender` compares every mapped frame through the actual native atlas
shader and CPU-baked reference. It requires GPU skinning, verifies lazy shader
creation, interleaved baked/rig draws, paint variants and restored GL state, then
exercises renderer resource recreation and forced CPU uploads. Run it with a
working OpenGL display; a software OpenGL driver is useful for correctness but
does not establish hardware performance:

```sh
python3 test/run_tests.py --binary unit --filter 'SkinModelRender/*'
```

The focused browser suite captures the production deformation shader's outputs
with WebGL2 transform feedback. It checks every frame of every clip in both the
analytic and translated-camera fixtures plus the installed worker against the
TypeScript evaluator, enforcing
0.05 logical-pixel position error and 0.001 normal-vector error. It records the
browser-reported renderer and measured errors as test attachments. The suite has
no API or built-app dependency; install the repository's Playwright browsers first:

```sh
npm exec --prefix platform -- playwright test -c platform/apps/web/e2e/rig.config.ts
```

All three engine projects (Chromium, Firefox and WebKit) must pass; unavailable
WebGL2 fails explicitly. Use `--project=chromium` for a focused iteration, and
record omitted engines. Browser-reported renderer strings may be masked and do
not establish physical hardware coverage. This suite checks the shared shader
body and evaluator, not the complete Emscripten game renderer, Studio interaction,
or browser context-loss recovery. Native resource recreation likewise does not
simulate operating-system context loss. Visual acceptance, those integration
paths, and the requested ten-pair gameplay performance budgets remain separate
release gates.

#### Paired rig frame measurements

Build `scons release=1 server=0 skin-rig-benchmark skin-game-preview` for the
opt-in diagnostics. `tools/skins/benchmark_rigs.py` runs alternating baked/rig
processes, saves the exact commands, binary/source/asset identities, raw frame
samples, peak process RSS and host-load snapshots, then reports medians of
per-run percentiles and paired percentage changes. Its bootstrap intervals
resample complete run pairs, not individual correlated frames. Use at least ten
pairs for reported comparisons. Stop competing builds/tests before acceptance
measurements; samples collected under contention are exploratory.

Prepare an ignored asset directory with the shipped meshes and the reproducible
paint/material pair from `tools/skins/make_paint.py DIR --material mixed`. Include
both `worker-walk.gsk` and `worker-walk.gsr`. On Linux with a working hardware X11
OpenGL display:

```sh
python3 tools/skins/benchmark_rigs.py \
  --binary build/linux/client/release/src/skin-rig-benchmark \
  --assets artifacts/rig/assets --output artifacts/rig/renderer-runs \
  --pairs 10 --frames 240 --warmup 64 --units 512 2048
python3 tools/skins/benchmark_rigs.py --kind scene \
  --binary build/linux/client/release/src/skin-game-preview \
  --assets artifacts/rig/assets --save artifacts/rig/preview.game \
  --output artifacts/rig/scene-runs --pairs 10 --frames 240 --warmup 64
```

The worker-only diagnostic draws an exact count of sprites at 1280×960 across
four paints. Its default 32 phases per paint give 128 distinct requests per
frame; `--phases 128` increases that to 512 when enough sprites are present.
Sprites repeat poses above the working-set size. At 2,048 sprites the display
shrinks each sprite to fit, so compare baked and rig within each population,
not pixel cost between populations. The warmed case preloads all 1,024
pose/paint combinations. The forced-miss case clears only atlas lookup entries
before each frame, retaining shaders, palette state, rest buffers and paint
textures. An assertion verifies the exact number of rasterizations. These
cases bound cache reuse and miss costs; neither estimates a played match's
actual miss frequency.

Frame samples include CPU submission and presentation with frame limiting and
swap interval disabled. When `GL_ARB_timer_query` is available, asynchronous
GPU elapsed queries cover the render commands, excluding presentation; their
results are read after the measured loop. Geometry/raster scopes measure CPU
and driver work. First draw and full-atlas population additionally wait for GPU
completion and are reported separately. Mesh load measures the normal loader
in a fresh process, without flushing OS file or driver shader caches. The mesh
byte count estimates owned element storage, not allocator/driver overhead or
all loader-retained copies; peak RSS measures the whole process.

The Scene case uses the existing mixed-unit saved-game diagnostic at 800×600
by default (`--scene-size WIDTHxHEIGHT`) and reports its actual added population.
The window must fit the desktop; a silently resized render target fails validation. It preserves per-frame simulation checksums for
comparison between paired runs. Animation phases change, but simulation does
not advance. Its `SKIN_BENCH_FORCE_MISS=1` switch applies the same atlas-only
invalidation; `SKIN_BENCH_UNCAPPED=1` disables presentation limiting. The runner
requires the intended rig shader and hardware renderer, and refuses a silent
CPU/software fallback. Scene measurements do not replace an active-match
playtest, browser integration, other physical platforms, or appearance review.
The Python runner currently targets Linux (`/usr/bin/time`, X11 and optional
NVIDIA telemetry); it is not an automated macOS acceptance runner.
