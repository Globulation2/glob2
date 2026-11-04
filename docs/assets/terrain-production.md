# Terrain production

Companion to [terrain materials](terrain-materials.md).

## Material production

All 37 built-in materials, including grass, sand, their lush, dry, dune, pale and
windy dune looks, road, ice and regular water,
are produced by `tools/artwork/terrain_synth.py`. Static materials have four
interchangeable 8×8 layouts (256 variants); lava and ember field have one 8×8
layout and water and deep water one 4×4 layout, keeping their animated banks
at 256 frames each. Frames are stored as
`content/data/gfx/terrain-<name>N.png` at 32×32 RGBA and
`content/data/highres/v1/terrain-<name>N.png` at 128×128 RGBA. Each has a provenance record
in `content-src/gfx/<name>/provenance.json`. The shared tile code in
`tools/artwork/material_tiles.py` provides sheet cutting, pixel hashing and style
statistics. The original legacy artwork remains available as style references;
new terrain frames use their own `terrain-` prefixes and do not overwrite it.
Use an isolated environment with the pinned asset-tool dependencies, including
NumPy and SciPy for exact indexed full-block Worley fields. Pillow's resampling kernels are a determinism
input:

```sh
uv venv artifacts/asset-tools/venv
uv pip install --python artifacts/asset-tools/venv/bin/python -r tools/artwork/requirements.txt
ENC=artifacts/asset-tools/venv/bin/python
"$ENC" tools/artwork/terrain_synth.py                       # write all procedural frames and their tileset.json blocks
"$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
"$ENC" tools/artwork/terrain_synth.py --emit-catalog        # material blocks and colour initialisers
"$ENC" tools/artwork/terrain_synth.py --write-catalog       # refresh tileset.json from committed frames (no render)
"$ENC" tools/artwork/validate_material.py --all
"$ENC" -m unittest discover -s tools/artwork -p 'test_*material*.py'
"$ENC" -m unittest discover -s tools/artwork -p test_terrain_synth.py
```

### Procedural originals

`tools/artwork/terrain_synth.py` synthesises ground-like materials from scratch:
periodic value noise (a random lattice tiled 3×3 and resized bicubically, so the
field wraps by construction), fractal sums, domain warps, Worley cells (F1 and
F2−F1, Euclidean and Chebyshev), scattered stamps and palette ramps. Named
geometry and animation random streams use deterministic `fnv1a32` seeds derived
from the material, layout and phase. Each layout renders a continuous field whose boundary geometry is
shared with the other layouts; its interior uses a separate seed. Existing tiles
are never read into an output; the original grass, sand, trail, ice and water
tiles are measured (luma mean and spread, wrapped neighbour grain, saturation)
for the style targets that preserve their low-contrast, painterly character:
ground materials aim at a luma spread of 6–14 and a grain of 3–10 at 32 px.

Ground recipes render periodic blocks and slice them into 128×128 frames,
area-averaged to 32×32 for classic rendering. Every block is 8×8 tiles
(1024×1024 HD pixels), with four interchangeable layouts. Synthesis uses one
worker per core, as many as physical memory allows (about 1.5 GB each for the
temporary field buffers); `--jobs` overrides that. Noise frequency,
stone counts and stamp counts scale with block size so feature sizes and density
stay at their native scale. Stamps can cross any cell edge and wrap around the
whole block. Shading and domain warps sample the whole block, including its
wrapped boundary, before it is sliced. One affine luma transform, computed from
the pooled native statistics, gives every frame the same tone.

All built-in recipes declare `periodic=True` and a `grid`; static recipes use
`grid=8, blocks=4`, lava and ember field `grid=8`, and the waters `grid=4`:
cell position selects the corresponding frame within one seeded layout, and
runtime and compiled packs skip border preparation. This includes object-based
grounds such as heath as well as the legacy bindings.
The legacy materials retain their established palettes and material identities;
water keeps its violet-blue palette, and ice keeps its frozen-ground profile.

The fallback for nonperiodic sources blends the outer four native pixels toward
frame 0's reflected border, with weights 1, 3/4, 1/2 and 1/4. Matching pixel values
this way does not preserve the contours or shading of independent source tiles:
it can produce a visible square frame even on soft surfaces such as mud and snow.
The procedural ground recipes therefore no longer use `neutral_band`,
`share_perimeter` or the runtime's reflected-border blend. Contact sheets use
positional neighbours for grid materials, including the block wrap, rather than
randomly shuffling their frames. The generator and material validator compare
boundary contrast with the upper range of interior line contrast at both native
and HD resolution. This covers the sampling phases of bicubic noise and
intentional boardwalk plank gaps without mistaking them for tile seams. A
synthetic block contract also verifies that slicing leaves every HD pixel
unchanged and that native frames equal downsampling the unsliced block.

Group readability is part of the recipes. Every type in a gameplay group has
the same rules, so each group carries one learnable mark, drawn by one shared
helper in `terrain_synth.py`, while colour and material stay free per biome:

| Group | Mark | Helper |
| --- | --- | --- |
| Grass | baseline, no mark | |
| Fertile (loam, moss, spring meadow) | warm, saturated ground | |
| Water, deep water, lava | reserved colour | |
| Snow and ice (ice, deep snow, frozen lake) | reserved cold white and pale teal | `warped_edges`, `bubbles` (frozen lake) |
| Sand | warm fine grain | `sand_look` |
| Barren (dirt, clay, shale, heath) | faded colour, prominent broken dry cracks one to two native pixels wide | `barren_mark`, `group_cracks`; `plates`, `jag`, `cast` (shale) |
| Paths (road, brick path, boardwalk, sandstone tiles) | laid, made surfaces: joints between setts, bricks, planks or flags, with a crisp, lighter worn lip | `worn_edge`, `herringbone` |
| Rough (mud, mudflat, scree, pumice field) | heavy relief about two native tiles across: lumps, curled plates or heaped slabs, with sunken pits | `lumps`, `wet_pits`; `rock_layer`, `scree_fields` (scree) |
| Ridges (ridge rock, outcrop) | long low ledges: a lit lip, then a short shaded riser | `ridge_mark` |
| Void (hole, chasm) | near-black depth; the hole casts onto every neighbour | |
| Obstacles | tall raised decor | `terrain_decor.py` |

Within the groups: dirt keeps the original speckled earth and stones under the
shared cracks; clay is red baked clay in bold plates; shale is big flat plates of grey stone lying over one another;
heath is clumps of dormant and dead heather cushions,
each built of lit sprig tufts, over pale cracked sandy peat. Paths are all laid by the
globs, never natural ground: road is stone setts in running-bond courses; brick
path is long clay pavers in an axis-aligned herringbone that
reads the same whichever way the path runs; boardwalk is long weathered planks
with grain, knots and nails; sandstone tiles are square flags, two by two per
cell, ochre and grey in a checker. Mud is churned lumps and water-filled pits;
mudflat is desiccated mud in big curled plates split by deep cracks,
with salt bloom and a few damp sunken plates; scree is big jagged slabs heaped
over one another, each tipped to its own brightness; pumice field is dark porous
volcanic rock riddled with vesicles, beside lava and ember field. Frozen lake
plays exactly like ice: frosted teal lake ice with a few long white pressure
cracks, trapped bubbles and frost patches, static and paler than any water. The helpers keep the block contract: geometry comes from
the shared stream at the block edge, interiors vary, and any normalisation or
lighting uses the common reference. Obstacles are lit from the top left and cast
onto grass, and void casts onto every neighbour. The grass and sand
looks keep the grain of the classic tile they stand in for and change only its
colour: lush and dry grass are soft like grass, dune and pale sand speckled like
sand. Windy sand dune keeps that grain and sweeps it into wind ripples
(`ripple_light`): long lit windward slopes, a bright crest and a short shaded lee,
with crests square to the water's main swell (`RIPPLE_WIND` matches `WAVE_SWELL`)
and a second ripple family that breaks them into forks and ends. All four sands use lit grains one or two native pixels across, half the
size of the original sand tile's grains. Like grass and sand they are four-layout 8×8 block
materials, and they take the same pair treatments (`GRASS_LOOKS` and `SAND_LOOKS`
in the synthesiser).

### Raised obstacle decor

Obstacle terrain is drawn in two layers, as forest is. The ground material is
composed like any other terrain: dusty earth for boulders, leaf litter for hedge,
undergrowth for thicket, and rock for ridge rock and outcrop. A decor sprite per
cell is then drawn with the resources, row by row, so objects stand up, overlap
neighbouring cells and occlude each other.

A material opts in with a `decor` block:

```json
"decor": {"sprite": "content/data/gfx/terrain-decor", "full": [0, 1, 2], "edge": [8, 9]}
```

- A cell draws raised decor only when every corner resolves to a decorated
  material. An undecorated corner, such as ordinary grass, sand or water, suppresses all
  raised objects in that cell, preserving a clear border against open ground.
  Eligibility follows rendered material bindings, including custom materials.
- Uniform cells keep their existing `full` or connected mask-15 frames and
  interior variants. A border between decorated materials draws just one
  material's ornament: a deterministic coordinate/terrain-seed hash chooses a
  corner, so two-plus-two corners give each material equal odds and three-plus-one
  corners give a three-to-one split. This uses no simulation random numbers.
- Mixed decorated cells use the selected material's `edge` frames, including
  when it occupies just one corner. Connected foliage retains its authored
  corner geometry; adjacent connected foliage materials join into one crown of
  the selected material. No overlapping ornaments or new artwork are needed.
- Connected arrays have 16 mask groups with equal variant counts (top-left bit 1,
  top-right bit 2, bottom-left bit 4, bottom-right bit 8). `connected_corners`
  retains its seeded corner-colour variants; `connected_interior` supplies
  alternative filled-cell crowns.
- The variant is chosen by a coordinate hash salted with the map's terrain seed,
  unless `connected_corners` is true. Then each mask holds 16 corner tiles: every
  map vertex has one seeded colour bit, shared by all materials, and a cell
  draws the tile for its four corner colours. Crown clusters are anchored on
  vertices and keep within a band along the cell edges, so neighbouring frames
  agree wherever they meet and large patches neither seam nor repeat
  periodically. Optional `connected_interior` frames (colour-major, the same
  count per colour) give filled cells alternative interior crowns, chosen by a
  cell hash; interior crowns stay clear of the cell edge. Connected foliage
  also joins other connected foliage materials, avoiding bare strips between
  hedge and thicket.
- All decor blocks share one sprite, so the cached GPU path batches decor rows
  like resource rows.
- Frames are at most 64×64 and centred on the 32×32 cell. Connected foliage
  uses cropped 32×48 frames to limit texture memory. Object silhouettes
  may overhang neighbours; connected canopy surfaces join at cell boundaries.

`tools/artwork/terrain_decor.py` synthesises the frames in the game's soft
three-quarter view:

- Each object stands on a base point and rises into the cell above.
- The body has a lit top and a darker front face, with edge shading only on the
  side away from the light.
- One soft ground shadow falls to the lower right.

Hedge and thicket use connected canopy surfaces rather than shaded bushes
stamped on every cell. Only exposed contours receive front-edge shading;
interior cells have no ground shadow or dark rim. Exposed fronts have a short
foliage skirt and contact shadow. The ground compositor skips their ground
seam cast, so a second shadow cannot protrude from the rough undergrowth
contour as dark teeth or hairlines. Their `canopy` ground profile has no
roughness, speckle or contour displacement; irregularity belongs to the
foliage silhouette rather than exposed soil spikes. `--write-catalog` installs
this profile and applies it to foliage material pairs as well as their decor. Hedge has dense green leaf
strokes and soft crowns. Thicket has sparser olive foliage with visible branches.
Both rise six native pixels above their ground material and sample one
continuous canopy texture across a 4×4 block of cells. Thicket has broader
boundary irregularity and woody gaps between crowns. The other sets are faceted boulders and rubble, clusters of faceted crags along
a shared grain, and broad stepped bedrock blocks with sparse lichen. Ridge and
outcrop rocks are flat-shaded prisms: a convex ring raised into walls, with
sloping top facets meeting at an off-centre peak, so each face takes one light
value. The tool writes `content/data/gfx/terrain-decorN.png`
(legacy set k at frames 12k–12k+11; hedge corner tiles at 60–315 and interiors at
316–427; thicket corner tiles at 428–683 and interiors at 684–795), the HD frames, provenance and, with
`--write-catalog`, the `decor` blocks. `--sheet` writes a review sheet. Select `--decor hedge --decor thicket --sheet <path>`
for before/after fields with narrow strips, corners, isolated vegetation and a
hole. Decor is presentation only: the minimap, the overview and
the simulation ignore it.

### Periodic edges

The runtime blend above ghosts textures built from discrete objects (pebbles,
flower heads, tussocks). All built-in materials instead set
`"edges": "periodic"` (recipe flag `periodic=True`) and render complete blocks.
Within a block, objects can cross cell edges freely. Across interchangeable
block boundaries, recipes preserve shared periodic object geometry and vary the
interior. Object fields, stamps and domes wrap across the complete canvas, and
shared scalar fields supply continuous ground, shadows and lighting. Heath uses
this approach too, rather than repeating a shared object band around every
individual tile: its heather cushions are whole cells, and Voronoi-based marks
(cracks, shale plates, lumps, mudflat plates, scree fragments) keep their
boundary cells in every layout.

Lava and ember field are animated: one 8×8 layout with four phases, frame
`variant + 64 * phase` (frames 0–255), catalogued with `animation_frames 4`,
`animation_stride 64` and `animation_ticks 8`. The crust layout is shared by the
phases; only the glow ramp moves.

### Positional variants

`"variant_grid": G` (a power of two up to 16, with `"edges": "periodic"` and
G×G variants per layout) picks a cell's variant from its position instead of the
hash: cell (x, y) shows variant `(y mod G) * G + (x mod G)`, and variant
weights are ignored. The variants are then one periodic block G cells across
that repeats over the map; map sizes are powers of two no smaller than G, so the
block also wraps across the torus seams. The map seed does not move the cell
grid. With a single layout, each variant meets only its block neighbours, so its
edges continue theirs rather than a shared band, and the border blend is skipped
as for periodic edges. Use it for a field that must be larger than one cell, such
as travelling waves. Map artwork bundles do not accept the key, so maps that
carry custom art still load in older clients.

#### Interchangeable blocks

Every static built-in material uses four interchangeable 8×8 blocks with
`"variant_blocks": 4` alongside `"variant_grid": 8`. Each layout has 64 cells,
so the material contains 256 variants in block-major, row-major order:
`cell + 64 * block`. The renderer hashes the canonical map block coordinates
with the material and map-seed salts for each 8×8 block. Every cell in that block
uses the same layout, preserving
its interior joins; different seeds produce different arrangements. A missing
`variant_blocks` retains the original single-block selection. The compiler and
runtime require a complete grid for each declared block. Custom map artwork
bundles continue to exclude these positional keys.

The layouts share actual periodic boundary geometry. Mud and snow interpolate
scalar height, grain and pool/sparkle fields toward independent interiors, with
one common normalization range. Scree retains boundary stones and replaces only
interior Voronoi points. Its shared-stone guard is derived from the maximum
second-nearest boundary distance and the angular metric’s Euclidean lower
bound, so a changed interior stone cannot influence a shared edge. Unrelated
RGB rock images are never cross-faded.
Interior variation begins beyond 32 HD pixels from the block perimeter and
reaches full strength at 96 pixels. Lighting uses a common reference gradient
scale, and the final style transform pools every layout, so neither shading nor
normalization changes the common boundary colors. The validator checks identical
outer strips across all layouts at native and HD resolution, distinct interiors,
and continuity inside each block. Matching strips guarantee every ordered block
pair and four-block junction uses the same continuous boundary, including map
wraparound. The source bank contains four complete layouts, while runtime
preparation loads only variants needed by the visible cells; see
[Visible material preparation](#visible-material-preparation). The 8×8 size delays
repetition of the shared edge motifs while retaining the same tile dimensions
and feature density. Shared boundary motifs can still become recognizable;
judge repetition in mixed-block previews.

Regular water (`terrain_synth.py` recipe `water`, sprite `content/data/gfx/terrain-water`)
keeps the violet-blue of the retired scrolling ocean image (about (69, 52, 200)).
Water and deep water use one 4×4 layout with sixteen phases
(`animation_ticks 6`, preserving the 96-tick loop). Each phase renders 512×512
periodic fields from plane waves whose wave numbers and animation cycles are
integers (`WAVE_GRID` sets the block size; wave numbers scale with it).
Every cell animates locally on the shared clock, so swell crests roll toward the
lower right and finer cross chop breaks them into moving glints. Deep water
reads the same geometry with its darker palette and less chop.

Both depths read the same wave geometry and timing on the same position grid,
so crests continue across the shallow/deep blend. Every layout multiplies by the
phase count, so the bank size decides whether an animated material can take
several layouts. Four 8×8 layouts at four phases (lava, ember field) are 1024
frames and fit: the renderer keeps every phase of the visible variants resident
(see [Visible material preparation](#visible-material-preparation)) and
`animation_blend_steps` limits how often their pages recompose. Four layouts at
sixteen phases would be 4096 frames per water, which doubled frame time and kept
HD fallback visible in measurement, so water and deep water stay on one layout.
Give a multi-layout animated material `animation_blend_steps` (4 for an
8-tick phase) and compare its frame time and HD fallback with the single-layout
material in a release build before committing it. `"variant_block_group"`
remains available for materials that share block selection. Dark water remains static. All three waters are opaque and use
the ordinary material composition path.

Each sixteen-phase water has 256 native and 256 HD frames, indexed as
`cell + 16 * phase`; its catalog animation stride is 16. Animated
materials crossfade on every visual tick and recompose the terrain pages that
show them at that cadence; see
[Caches and compatibility](terrain-rendering.md#caches-and-compatibility) for the
kept coverage that avoids resampling boundaries.

#### Visible material preparation

The source bank's full frame count does not determine the active decoded pixel
set. These terrain banks retain CPU source images and upload a standalone source
texture only if it is drawn directly; composed pages own their drawing textures.
HD sprite frames load individually when requested, without preparing unused
source mip chains. Only materials with more than one layout defer their HD
frames this way (`Sprite::deferHighResolution`); single-layout materials,
including the animated ones, load HD with their sprite. Loading every block
material eagerly instead cost about 3 s of load time and 800 MB of memory on
a 256×256 map. Visible terrain
preparation gathers the material/variant combinations needed by the viewport,
including its composition boundary coverage, and prepares those combinations.
`Compositor::prepareVisible` derives this set from a scene and visible cell
bounds; `prepareForRecipes` accepts an explicit recipe set for callers that
already have it. Animated selections prepare the current and next phase for
interpolation. A streamed (multi-layout) animation also keeps the later phases
of its visible variants resident, within a 64 MiB look-ahead budget shared by
all animated materials (`Compositor::AnimationLookaheadBytes`). Endpoints decode
first, and an advance reuses resident frames instead of dropping the old phase
and decoding the next one again; without that, every phase change re-decoded
the whole visible set. When the budget is exhausted a material falls back to
current and next only. Unused variants, phases and HD sources are retired as the
selection changes; offscreen materials do not advance their prepared revisions
merely because the visual clock changes.

Published CPU material snapshots own their RGBA pixels. Retiring a source or
changing the active selection therefore leaves a retained snapshot usable by
an in-flight composition batch. CPU jobs snapshot only the recipes in each
bounded group, reducing the group size when necessary to stay within the existing
4 MiB in-flight reservation. Reloading unchanged native sources after eviction
does not invalidate already composed pages. Compiled-pack data and catalog metadata remain
part of the compositor's memory accounting, separately from the selected pixel
payload. This is a resource-lifetime contract, not a performance guarantee;
viewport size, sampling resolution and the number of visible material variants
still determine preparation work and live pixel storage.

Committed art is not re-rendered to check it: regenerate only the materials
you changed (`--material`), on any platform. Renders depend on Pillow's kernels
and the C library's `sin`/`atan2`/`hypot`, so another platform may differ in a
few low bits; `provenance.json` records the platform, generator hashes and
Pillow version for information only. Recipes swapped to image-generated art are
marked `placeholder_only=True`.
Writing frames also writes each variant's 128×128 render, the exact source the
classic tile is downsampled from, as its HD frame (`content/data/highres/v1/terrain-<name>N.png`
and `content-src/gfx/production/procedural-materials/`), registers it in the pack
manifest and `frames.txt` through `tools/artwork/highres_pack.py`, and records its
hash in the provenance. `--hd` alone writes
the renders under `artifacts/terrain/hd/` for review and changes nothing else.
Preview and minimap colours derive from the rendered mean unless a recipe
overrides them for legibility (void, hazards, deep water).

### Image-generated materials

The shipped built-in terrain uses procedural recipes. The older image-generated
workflow remains in `tools/artwork/export_material.py` for separately authored
materials: its prompts and provenance identify the reference images, generated
source and exporter, and `--animate-glow` preserves a generated crust while
varying its glow ramp. Its original sixteen-tile sheet workflow is not a
replacement for the built-in four-layout contract. Adopting external artwork for
a built-in material requires producing all declared layouts and phases,
preserving shared boundary geometry and updating its provenance and catalog
consistently. Inspect both native and HD block joins before shipping such a
replacement.

### Provenance layout

```
content-src/gfx/<name>/
  provenance.json   generator, method (procedural | image-generator | hybrid),
                    generator or exporter sha256, recipe or prompt, Python and
                    Pillow versions, style references ("statistics only"),
                    frame layout, runtime pixel sha256 per frame, `replaces`
  material.png      image-generated source only
```

`tools/terrain_tileset.py` copies a summary of each record into the compiled
pack's `provenance.materials`; legacy materials without a record are listed as
existing artwork.

### Boundary profiles for the catalogue

Each material names a border class and its edge profile;
[Boundary rules](terrain-rendering.md#boundary-rules) then choose every contact
without pair treatments. Classes, strongest first:

| Class | Materials | Edge profile |
| --- | --- | --- |
| `cover` | hedge, thicket | `canopy` |
| `cut` | void_hole, chasm | `crisp` |
| `built` | road, brick_path, sandstone, boardwalk | `paved`, `crisp` |
| `flow` | lava, ember_field | `molten` |
| `solid` | everything else | `rock`, `soil`, `frozen`, `brush`, `turf`, `sand` |
| `pool` (shoreline) | water, deep_water, dark_water | `still` |

| Profile | Shape | Precedence / coast | roughness / amplitude / feather / speckle / bridge (Q8) | Materials |
| --- | --- | --- | --- | --- |
| `canopy` | patch | 0 | 0 / 0 / 128 / 0 / 512 | hedge, thicket |
| `crisp` | patch | 20 | 96 / 256 / 128 / 0 / 256 | void_hole, chasm, boardwalk |
| `paved` | smooth, wiggle 256 / 128 px, squareness 224 | 10 | – / – / 224 / – / 384 | road, brick_path, sandstone |
| `track` | smooth, wiggle 768 / 64 px, squareness 128 | 0 | – / – / 320 / – / 512 | none since dirt track became brick path; kept for future tracks |
| `molten` | smooth, wiggle 1536 / 64 px | 0 | – / – / 320 / – / 512 | lava, ember_field |
| `still` | smooth, wiggle 1536 / 128 px | 0 | – / – / 320 / – / 640 | water, deep_water, dark_water |
| `rock` | contextual | 50 | 384 / 896 / 192 / 448 / 192 | boulders, ridge_rock, outcrop, scree, shale, pumice |
| `soil` | contextual | 40 / `shore` | 288 / 896 / 448 / 320 / 512 | dirt, clay, mud, mudflat, deep_snow, loam, moss |
| `frozen` | contextual | 30 | 320 / 768 / 320 / 128 / 256 | ice, frozen_lake |
| `brush` | contextual | 20 / `shore` | 320 / 768 / 320 / 384 / 512 | heath, spring_meadow |
| `turf` | contextual | 10 / `shore` | 256 / 1024 / 320 / 512 / 512 | grass, lush_grass, dry_grass |
| `sand` | contextual | 10 / `beach` | 256 / 1024 / 320 / 512 / 512 | sand, dune_sand, pale_sand |
| `shore` | contextual | coast only | 256 / 832 / 320 / 384 / 704 | grassy and earthen coasts |
| `beach` | contextual | coast only | 104 / 1024 / 448 / 0 / 640 | sandy coasts |

`tools/artwork/terrain_synth.py` records each material's `profile` and
`border_class` in its recipe and writes both into the catalog. The `rock`,
`crisp`, `brush` and `frozen` contours are seeded random walks from
`tools/terrain_profile_curves.py`; `sand` and `turf` trace every distinct edge
of the original grass/sand tiles, so the classic coast keeps its look with less
repetition. `shore`, `soil` and `beach` are sine series with a weak fundamental
plus a detrended ripple; the weak fundamental keeps edge crossings near the
vertex lattice so lone cells stay round. The shipped tables have been tuned
beyond what the script emits, so review its diff before `--write`.

To place a new terrain, pick the class that says what the surface is and the
edge profile that says how it shapes the contacts it wins; add a profile only
for a genuinely new edge character. A pair treatment is an escape hatch for an
exception the classes cannot express, not the way to tune a material.

### Seam ranks

| Rank | Materials | Cast (`cast_q8` / `cast_width_q8`) and fringe |
| --- | --- | --- |
| 8 | void_hole, chasm | 96 / 512: the hole darkens every neighbour, the only depth cue a near-black texture can carry |
| 7 | dark_water | 80 / 704 |
| 6 | deep_water | 72 / 640, a drop-off lip on shallow water and shores |
| 5 | water (existing) | 72 / 640 |
| 4 | boulders, hedge, thicket | 88 / 832, a sense of height on the grass side |
| 4 | ridge_rock, outcrop | 72 / 768 |
| 4 | lava, ember_field | no cast; scorch fringe `[214,110,40]` 128/640 and `[160,80,40]` 96/512 |
| 4 | ice (existing), frozen_lake | 64 / 640 with frost fringe |
| 3 | road (existing), brick_path, boardwalk, sandstone | 48 / 512 |
| 3 | deep_snow | 48 / 512 with cool fringe `[196,210,232]` 64/384, a rim on grass and sand |
| 2 | grass (existing), loam, moss, spring_meadow | 56 / 512 |
| 2 | heath | 48 / 480 |
| 2 | dirt, clay, shale | 40 / 448 |
| 1 | sand (existing), mud, mudflat, scree, pumice | no cast |

Casts stay within two to three pixels and near or under a third strength, as
[Seams](terrain-rendering.md#seams) requires. Equal ranks cast nothing, so barren ground beside grass
takes no lip. A lighter inner lip inside a hole cannot be expressed by the seam
model, which only tones neighbours; it would need an engine-side self-lip.

Related: [asset production](README.md).
