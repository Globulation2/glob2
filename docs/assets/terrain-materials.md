# Terrain materials

Detailed terrain rendering uses `data/terrain/tileset.json`. Gameplay continues to
use stable `TerrainType` identities and properties. Visual material handles, masks,
texture choices and composed pages are transient; they never enter saves, orders,
checksums or the synchronized random stream.

## Add artwork

A version-3 catalog contains `profiles`, `materials`, `bindings` and optional
`pair_treatments`. Version-1 packs remain readable with their original five-point
contours and sampling behavior; version 2 adds denser contours, edge softness and
world-space bends; version 3 adds per-profile displacement amplitude, pebble
speckle, diagonal bridging and up to sixty-four curves per profile. Version-2
packs parse unchanged and render through the current resolver, which rounds
corners and reads each curve mirrored and negated. Material keys are unique strings. Bindings map semantic terrain
names to material keys. Every paintable built-in terrain in `src/map/TerrainTypeTable.h`
requires a binding: the five legacy names (water, sand, grass, ice and road) plus the
terrain-catalogue names, which `tools/terrain_builtin_names.json` mirrors for the Python
validator. Several names may bind the same material. The catalogue materials behind those
names are produced as described in [Material production](#material-production). Adding
a visual material does not add gameplay rules. Runtime terrain definitions
reuse a shipped appearance binding with independently resolved simulation properties;
see [map authoring](../map-generators/GAME_RULES_FOR_MAP_DESIGN.md#authoring-additional-terrain-types) for the JSON import workflow. Runtime
definitions cannot introduce artwork or modify this catalog. Their canonical IDs
remain in maps and saves, while scene snapshots cache the shipped appearance IDs
used by the compositor.

Each material supplies:

- `key`, a stable authoring name, and `sprite`, a `data/`-relative sprite prefix;
- `variants`, an array of `{ "frame": 272, "weight": 1 }` entries;
- `profile`, the key of its boundary family, and `preview`, three overview RGB channels;
- optional `minimap` RGB channels for minimaps and thumbnails (defaults to `preview`);
- optional `animation_frames`, `animation_ticks`, and `animation_stride`;
- optional `edges` (`blend` or `periodic`, see [Periodic edges](#periodic-edges))
  and `variant_grid`, see [Positional variants](#positional-variants);
- optional `seam` (version 3) with `height`, `cast_q8`, `cast_width_q8`, `fringe`,
  `fringe_q8` and `fringe_width_q8`, see [Seams](#seams).

Every material, regular water included, is an ordinary opaque tile set. The
retired `ocean` and `backdrop` keys are rejected: water used to be a transparent
hole over a separately scrolled 512×512 image, which kept it from blending with
its variants.

Logical frames are 32×32. Existing HD frame registration supplies higher resolution
source textures when available; missing HD artwork uses native pixels. Positive
integer weights select interior variants deterministically from canonical cell
coordinates and the material key. Reordering material definitions does not reseed
variation. Changing a key deliberately changes its visual seed.

To add more variety to an existing terrain, add frames and weights to that
material. To replace its appearance, add a material and change the corresponding
binding. These edits require no new renderer switch statement or save migration.
The shipped catalog reuses the existing sixteen interior variants per terrain. The
`road` binding is terrain ID 4, now named Trail by the engine. This pack retains
the requested cobblestone appearance in `data/gfx/terrain-cobblestone0…15.png`,
copied unchanged from frames 288–303 at revision
`e1634ecda9a2a2d31f47dfe766ddbcb40e364791`. The engine's newer dirt Trail textures
remain in those original saved-frame slots. To use them visually, set the `road`
material's sprite back to `data/gfx/terrain` and its variants to frames 288–303;
no engine, save or renderer change is needed.

### Example: change ice artwork without adding gameplay rules

Add `data/gfx/tundra0.png` and `data/gfx/tundra1.png`, both 32×32 RGBA images.
Append this entry to the catalog's `materials` array, reusing the existing
`fractured` profile key:

```json
{
  "key": "tundra",
  "sprite": "data/gfx/tundra",
  "profile": "fractured",
  "preview": [155, 205, 220],
  "minimap": [170, 210, 225],
  "variants": [
    {"frame": 0, "weight": 3},
    {"frame": 1, "weight": 1}
  ]
}
```

Set `bindings.ice` to `"tundra"`, keeping the other required bindings. The first
variant is selected about three times as often as the second over a large map;
selection is stable for a coordinate and material key. The first variant also
supplies the family's common texture border, so choose a representative texture.
To add a third variant, create `tundra2.png` and append its frame and positive
weight. Sprite prefixes use the normal contiguous frame sequence starting at 0;
a material can reference a subset of that loaded sequence.

Material keys and profile keys must be nonempty and unique in their respective
arrays. Frame indices are 0–65535; individual weights are 1–1,000,000 and their sum
must not exceed 1,000,000,000. RGB channels are integers 0–255. The compiler checks
all required and additional bindings against the registered materials. Paths must
remain under `data/`, with forward slashes, canonical segments (no empty, `.` or
`..` segments), and no drive prefixes.

For animation, a variant's effective frame is `frame + phase * animation_stride`.
`animation_frames` is 1–256, `animation_ticks` is a positive integer duration in the
renderer's animation clock, and the stride must be positive when there is more
than one phase. Every effective frame must exist and remain below 65536.
All cells of a material share the phase, so a texture that moves must move the
same way in every variant. Between phases the renderer linearly crossfades the
current and next textures using the remainder of `animation_ticks`, including
last-to-first at the loop boundary. It keeps two prepared endpoints and one
blended texture per variant, without adding sprite frames. The existing visual
clock and pause behavior are unchanged. Materials with a one-tick phase duration
still select a discrete frame.

## Material production

The twenty-four catalogue materials (`boulders`, `hedge`, `thicket`, `ridge_rock`,
`outcrop`, `dirt`, `clay`, `gravel`, `flower_meadow`, `mud`, `marsh`, `deep_snow`,
`scree`, `dirt_track`, `boardwalk`, `lava`, `ember_field`, `loam`, `moss`,
`spring_meadow`, `deep_water`, `dark_water`, `void_hole`, `chasm`) ship as
`data/gfx/terrain-<name>0..15.png`, sixteen 32×32 RGBA variants per material, with
a provenance record in `datasrc/gfx/<name>/provenance.json`. Two production
methods share the tile code in `tools/artwork/material_tiles.py` (sheet cutting,
perimeter sharing on premultiplied RGBA, pixel hashing, style statistics), so a
material can move from one method to the other without changing its contract.
Run the tools with the pinned asset encoder interpreter, since Pillow's
resampling kernels are a determinism input:

```sh
ENC="$(python3 tools/package_assets.py --encoder-python)"
"$ENC" tools/artwork/terrain_synth.py                       # write all procedural frames
"$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
"$ENC" tools/artwork/terrain_synth.py --check               # reproduce and compare
"$ENC" tools/artwork/terrain_synth.py --emit-catalog        # material blocks and colour initialisers
"$ENC" tools/artwork/terrain_synth.py --write-catalog       # merge them into tileset.json
"$ENC" tools/artwork/export_material.py --name boulders     # image-generated material
"$ENC" tools/artwork/validate_material.py --all
"$ENC" -m unittest discover -s tools/artwork -p 'test_*material*.py'
"$ENC" -m unittest discover -s tools/artwork -p test_terrain_synth.py
```

### Procedural originals

`tools/artwork/terrain_synth.py` synthesises ground-like materials from scratch:
periodic value noise (a random lattice tiled 3×3 and resized bicubically, so the
field wraps by construction), fractal sums, domain warps, Worley cells (F1 and
F2−F1, Euclidean and Chebyshev), scattered stamps and palette ramps. Every random
value comes from `random.Random(seed).random()` with
`seed = fnv1a32("<material>:<variant>:<phase>")`, so the sixteen variants are
independent syntheses and the output is byte-identical across runs. Existing tiles
are never read into an output; the native grass, sand, trail, ice and water tiles
are only measured (luma mean and spread, wrapped neighbour grain, saturation) for
the style targets that keep the new art low-contrast and painterly beside them:
ground materials aim at a luma spread of 6–14 and a grain of 3–10 at 32 px.

Each recipe renders at 128×128, is area-averaged to 32×32 and pulled toward its
luma targets by one affine transform computed from the pooled statistics of all
sixteen variants, so all variants of a material share one tone. Periodic and grid
materials (below) skip the border preparation and none of the perimeter treatment
in this paragraph applies to them. Joins work with the runtime's border preparation
rather than against it: `seamless_sources` (and `TerrainCompositor::prepare`)
blend the outer four native pixels of every variant toward variant 0's
*reflected* pixel (`min(x, 31 - x)`, `min(y, 31 - y)`) with weights 1, 3/4, 1/2
and 1/4, so whatever variant 0 carries on its perimeter is repeated on every
tile. The synthesiser therefore makes that perimeter ordinary rather than
special: the render with the quietest perimeter (lowest luma spread in the
outer four native pixels) takes frame 0, stamped features (stones, twigs,
blooms, embers) stay at least three native pixels from every edge, and
`neutral_band` pulls only the very low frequencies (a radius-10 box blur at
render scale) of the outer two to three native pixels toward the tile mean,
tapering to zero inward, so no light or dark blotch sits on an edge while grain,
chips and colour variation remain for the runtime blend to land on. Replacing
the edge texture with a flat tone was tried and rejected: it reads as a frame
around every tile. `share_perimeter` then copies ring 0 from tile 0 (opposite
edges read the same coordinates, so a tile also joins itself across the torus
seam) and blends ring 1. The contact sheet shows a 3x3 field of random variants
at 1x and 2x after the runtime blend for every material; materials with hard
structure (hedge, scree, chasm, lava's placeholder) still show a faint seam at
2x, which is the cost of the runtime contract. Group readability is part of the
recipes: obstacles are lit from the top left and cast onto grass, hazards warm
and saturated, fertile ground warm and saturated (umber loam, moss, meadow),
barren ground muted warm neutrals, rough ground cool and desaturated, paths warm
and light, void near black with a cast onto every neighbour.

### Raised obstacle decor

Obstacle terrain is drawn in two layers, as forest is. The ground material is
composed like any other terrain: dusty earth for boulders, leaf litter for hedge,
undergrowth for thicket, and rock for ridge rock and outcrop. A decor sprite per
cell is then drawn with the resources, row by row, so objects stand up, overlap
neighbouring cells and occlude each other.

A material opts in with a `decor` block:

```json
"decor": {"sprite": "data/gfx/terrain-decor", "full": [0, 1, 2], "edge": [8, 9]}
```

- `full` frames are used for cells whose four corners share a decorated
  appearance.
- `edge` frames are smaller and pulled toward the cell centre, for cells where two
  or three corners share it, so clusters do not spill far onto open ground. A
  single decorated corner draws no decor.
- The frame is chosen by a coordinate hash salted with the map's terrain seed.
- All decor blocks share one sprite, so the cached GPU path batches decor rows
  like resource rows.
- Frames are at most 64×64; they are centred on the cell, and the shipped 48×48
  frames overhang neighbours by 8 px.

`tools/artwork/terrain_decor.py` synthesises the frames in the game's soft
three-quarter view:

- Each object stands on a base point and rises into the cell above.
- The body has a lit top and a darker front face, with edge shading only on the
  side away from the light.
- One soft ground shadow falls to the lower right.

The sets are rounded boulders, a continuous hedge mass that joins its
neighbours, scrubby bushes with twigs, tilted ridge slabs and shards, and
bedrock massifs with sparse lichen. The tool writes `data/gfx/terrain-decorN.png`
(set k at frames 12k–12k+11), the HD frames, provenance and, with
`--write-catalog`, the `decor` blocks. `--check` reproduces them, and `--sheet`
writes a review sheet. Decor is presentation only: the minimap, the overview and
the simulation ignore it.

### Periodic edges

The runtime blend above ghosts any texture built from discrete objects (pebbles,
flower heads, tussocks): within four native pixels of every edge two unrelated
layouts are averaged. Materials whose look depends on such objects instead set
`"edges": "periodic"` (recipe flag `periodic=True`). Their variants share one
periodic outer band, so variant A's right edge continues into variant B's left
edge exactly as a single tile wraps onto itself, and both the compiler and
`TerrainCompositor::prepare` skip the border blend for them. In the synthesiser,
`object_field` places round objects in two sets. Objects that touch the outer
band come from the material's shared `base_rng`, are identical in every variant
and wrap across the edge. Each variant adds its own objects entirely inside the
interior. The shared quota follows the band's share of the tile area, so the
band is no denser or sparser than the interior and draws no grid.
`force_periodic_band` copies variant 0's outer four render pixels into every
variant as a guard. `neutral_band` and `share_perimeter` are skipped. Stamps and
domes wrap across the canvas edge. Gravel, flower meadow and marsh use this mode.
Their recipes follow reference photographs of pebble beds, wildflower meadows and
tussock bogs:

- Gravel is overlapping rounded pebbles of mixed grey and warm tones with contact
  shadows over a dark bed of fines.
- Flower meadow has patches of one colour plus singles, each head a ring of
  saturated petals around a contrasting centre with a soft shadow.
- Marsh has grass tussocks with radiating blades on dark wet moss, with open
  pools and reed tufts.

The photographs are only looked at; nothing from them is read into an output.

Lava and ember field are animated: four phases per variant, frame
`variant + 16 * phase` (frames 0–63), catalogued with `animation_frames 4`,
`animation_stride 16` and `animation_ticks 8`. The crust layout is shared by the
phases; only the glow ramp moves.

### Positional variants

`"variant_grid": G` (a power of two up to 16, with `"edges": "periodic"` and
exactly G×G variants) picks a cell's variant from its position instead of the
hash: cell (x, y) shows variant `(y mod G) * G + (x mod G)`, and variant
weights are ignored. The variants are then one periodic block G cells across
that repeats over the map; map sizes are powers of two no smaller than G, so the
block also wraps across the torus seams. The map seed does not move the block. Each variant only ever meets its block neighbours, so its
edges continue theirs rather than a shared band, and the border blend is skipped
as for periodic edges. Use it for a field that must be larger than one cell, such
as travelling waves. Map artwork bundles do not accept the key, so maps that
carry custom art still load in older clients.

Regular water (`terrain_synth.py` recipe `water`, sprite `data/gfx/terrain-water`)
keeps the violet-blue of the retired scrolling ocean image (about (69, 52, 200)).
Water and deep water are `variant_grid 4` materials with sixteen phases
(`animation_ticks 6`, the same 96-tick loop as the four-phase water before). The
recipe renders one 512×512 periodic block, a sum of plane waves whose wave
numbers are whole cycles per block and whose phases advance a whole number of
cycles per loop (at most four, a quarter wavelength per phase, so fine chop
moves rather than flickers), and slices it into the sixteen variants. Every cell animates
locally on the shared clock and the field is continuous across all cell edges,
so the swell's crests roll toward the lower right across open water, and finer
cross chop breaks them into moving glints. Deep water reads the same field in a
darker value with less chop, so crests carry on across the shallow/deep blend.
The visible pattern repeats every four cells instead of every cell. Dark water
is static. All three are opaque and blend through the ordinary soft profile.
Marsh pools are opaque too.

Each sixteen-phase material is 256 native and 256 HD frames. Animated materials
crossfade on every visual tick and recompose the terrain pages that show them at
that cadence; see [Caches and compatibility](#caches-and-compatibility) for the
kept coverage that avoids resampling boundaries, and check the terrain cache cost
when adding animation over larger areas.

`--check` re-synthesises every material and compares the pixel hashes with the
committed PNGs and with `provenance.json`; it also requires the recorded
generator hashes and Pillow 12.2.0, so any edit to `terrain_synth.py` or
`material_tiles.py` is followed by a default run that refreshes the provenance
(pixels that did not change produce byte-identical PNGs). Pixel-exact
reproduction is pinned to the encoder interpreter on Linux x86-64: besides
Pillow's kernels the renders depend on the C library's `sin`/`atan2`/`hypot` and
on `round()` boundaries, so the provenance records the platform and a mismatch
on another platform is reported with that note. Recipes swapped to
image-generated art are marked `placeholder_only=True` and skipped by `--check`.
Writing frames also writes each variant's 128×128 render, the exact source the
classic tile is downsampled from, as its HD frame (`data/highres/v1/terrain-<name>N.png`
and `datasrc/gfx/production/procedural-materials/`), registers it in the pack
manifest and `frames.txt` through `tools/artwork/highres_pack.py`, and records its
hash in the provenance; `--check` compares both resolutions. `--hd` alone writes
the renders under `artifacts/terrain/hd/` for review and changes nothing else.
Preview and minimap colours derive from the rendered mean unless a recipe
overrides them for legibility (void, hazards, deep water).

### Image-generated materials

Silhouette-heavy materials (boulders, hedge, thicket, lava, ember field, flower
meadow, outcrop) are produced with an external image generator from the prompts
in `MATERIAL_PROMPTS` in `tools/artwork/export_material.py`
(`--name <name> --prompt` prints one). The prompts share a header (seamless
top-down material for sixteen 32×32 tiles, painterly pastel-earthy low contrast,
flat lighting, no borders or directional shapes, readable after downscaling, the
four reference images grass, sand, trail and inn) and a body per material. Save
the selected image as `datasrc/gfx/<name>/material.png` (square, at least 512 px)
and run `export_material.py --name <name>`: it area-averages the image to the
128×128 sheet, cuts sixteen tiles, shares the perimeter and writes the frames
with `method: "image-generator"` provenance (prompt, reference and source hashes,
runtime pixel hashes, and a `replaces` record naming the procedural provenance it
supersedes). Lava and ember field must be exported with `--animate-glow`: it
keeps the generated crust and channel layout and applies the recipe's glow ramp
to the warm pixels for four phases (`method: "hybrid"`); exporting an animated
recipe without it, or a static one with it, is refused so the catalog's
`animation_frames` never points at stale frames. Then mark the recipe
`placeholder_only=True` in `terrain_synth.py` and run
`validate_material.py --name <name>`, which also checks that the recorded phase
count matches the recipe for every method.

### Provenance layout

```
datasrc/gfx/<name>/
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

| Profile | roughness / amplitude / feather / speckle / bridge (Q8) | Curves | Materials |
| --- | --- | --- | --- |
| `rock` | 384 / 896 / 192 / 448 / 192 | 48 × 9, angular | boulders, ridge_rock, outcrop, scree, gravel, chasm |
| `soft` | 160 / 512 / 448 / 128 / 640 | 40 × 17, gentle | mud, marsh, loam, moss, deep_snow, deep_water, dark_water, clay, dirt |
| `crisp` | 96 / 256 / 128 / 0 / 256 | 24 × 9 | void_hole, boardwalk |
| `brush` | 320 / 768 / 320 / 384 / 512 | 48 × 17 | hedge, thicket, flower_meadow, spring_meadow |
| `sand` (existing) | 256 / 1024 / 320 / 512 / 512 | 61 × 17, traced | water, sand, grass, dirt_track |
| `fractured` (existing) | 320 / 768 / 320 / 128 / 256 | 32 × 9 | ice, lava, ember_field |
| `cobblestone` (existing) | 256 / 640 / 224 / 320 / 384 | 24 × 9 | road |
| `shore` | 256 / 832 / 320 / 384 / 704 | 48 × 33, lobed with ripple | pair treatments only |
| `liquid` | 256 / 1024 / 448 / 128 / 640 | 40 × 33, broad and smooth | pair treatments only |
| `organic` | 288 / 896 / 320 / 320 / 512 | 48 × 17, lobed and noisy | pair treatments only |

`tools/terrain_profile_curves.py --write` generates the curves with seeded random
walks and appends a missing profile with these parameters; never author curves by
hand. The script still emits the earlier, shorter curve sets and lacks the
`shore`, `liquid` and `organic` profiles, so `--write` would shrink the shipped
sets; bring it up to date before regenerating. `sand` uses every distinct traced
edge of the original grass/sand tiles, so the classic coast keeps its look with
less repetition. `shore`, `liquid` and
`organic` are sine series with a weak fundamental plus a detrended ripple; the
weak fundamental keeps edge crossings near the vertex lattice so lone cells stay
round. Their feather and speckle are unused, since softness and pebbles come from
the material's own profile.

Pair treatments cover material pairs that meet inside one cell now that terrain
is stored per vertex. Entries are needed only where the choice differs from the
default rule (the rougher profile wins):

- `rock`: boulders, ridge_rock and outcrop against grass.
- `brush`: hedge and thicket against grass.
- `shore`: grass, moss, loam, spring_meadow and flower_meadow against water,
  deep_water and dark_water.
- `liquid`: water/deep_water, deep_water/dark_water, water/dark_water, ice/water,
  and marsh and mud against the three waters.
- `organic`: moss, loam, mud, marsh, dirt, clay and deep_snow against grass and
  sand; moss/loam, moss/marsh, loam/dirt, loam/mud, mud/marsh, dirt/clay,
  dirt/mud, clay/mud, deep_snow/ice, and spring_meadow against moss, loam,
  flower_meadow and dirt, and flower_meadow/dirt.
- `crisp`: boardwalk against grass, sand, the three waters, marsh, mud, moss,
  dirt, loam, clay, gravel, dirt_track and road; void_hole and chasm against
  every other material.
- `cobblestone`: road against grass, sand, dirt_track, gravel, scree,
  flower_meadow and spring_meadow.
- `fractured`: lava and ember_field against grass, sand, dirt, gravel and scree;
  lava against hedge and flower_meadow.

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
| 4 | ice (existing) | 64 / 640 with frost fringe |
| 3 | road (existing), dirt_track, boardwalk | 48 / 512 |
| 3 | deep_snow | 48 / 512 with cool fringe `[196,210,232]` 64/384, a rim on grass and sand |
| 2 | grass (existing), loam, moss, spring_meadow, flower_meadow | 56 / 512 |
| 2 | dirt, clay, gravel | 40 / 448 |
| 1 | sand (existing), mud, marsh, scree | no cast |

Casts stay within two to three pixels and near or under a third strength, as
[Seams](#seams) requires. Equal ranks cast nothing, so barren ground beside grass
takes no lip. A lighter inner lip inside a hole cannot be expressed by the seam
model, which only tones neighbours; it would need an engine-side self-lip.

## Boundaries and masks

The presentation resolver uses a 16-pixel lattice. Terrain is stored per map
vertex, so a tile's four corner vertices give its TL/TR/BL/BR materials directly; a
tile whose corners agree fills all four quadrants with one material. The resolver
reads the scene snapshot, never the live simulation. `PreparedCoverage` resolves
the nine patches needed by a tile once, including shared contour choices and
side-connected corner groups, then samples them at native or HD pixel centers.

The optional catalog-level `boundary_warp_q8` array controls world-space bends at
64-, 32- and 8-pixel scales. The shipped values `[960, 352, 120]` allow about
five and a half pixels of combined displacement per axis: a broad meander, a
medium ripple and a faint angular grit. The first two scales interpolate smoothly; the finest adds
angular irregularity. Pebbly detail comes from the authored profiles below. These bends continue across tile
boundaries instead of restarting a motif in every patch. Values are nonnegative
integers, bounded by `[1024, 384, 128]`; omitting the array disables the field
for older packs. Try reducing the first value for straighter edges, or the last
for less fine detail.

All materials share this field so multi-material junctions remain joined. Wrapped
world coordinates determine its control points, independently of texture variants,
camera position, animation and simulation randomness.

### Contextual natural borders

Version-3 profiles may set `"shape": "contextual"`; omission or `"patch"` retains
existing patch geometry. The shipped sand, shore, organic, soft, rock, brush,
liquid and frozen profiles use contextual geometry. Ice uses frozen, which shares
fractured's authored detail but enables natural curves; lava retains fractured.
Chasm uses the cliff profile, which preserves rock's authored detail with patch
geometry and prevents new natural materials from rounding a chasm boundary.
Both materials' own profiles and their selected pair treatment must opt in.
Constructed paths, lava, and crisp holes/chasm borders therefore retain their
existing treatment even against a rougher natural material.

The compositor reads a 4×4 vertex neighbourhood, including one vertex beyond each
side of the cell. Ordinary two-material cells use marching-square edge midpoints
and cubic curves with tangents guided by the adjacent cells' contour endpoints.
Handles start at one-third of the shorter adjoining segment. Their control hull
stays inside the cell and within four logical pixels of the straight contour;
non-monotone handles fall back to a straight contour. Ambiguous diagonal cells and
three/four-material junctions retain the patch resolver and its connection choice.
Uniform cells remain uniform, and neighbours never add materials to a cell's palette.

The guided curves also carry smooth seeded variation at three scales: broad
uneven lobes, smaller scallops and fine edge undulations. The material pair,
cell position and map look seed select the pattern; profile roughness scales
its strength. Detail tapers to zero displacement and slope at the endpoints.
Only the secondary coordinate moves, preserving monotonicity and connected
regions, and the combined smoothing and scalloping stays within a
ten-pixel curve budget. Sixty-four segments resolve the smallest scallops.
Corner clearance limits deep lobes near the cell's corners and relaxes smoothly
towards its centre, preserving small terrain pockets and narrow strips.

Curves are sampled into a monotone row/column table once per cell. Native, HD and
overview samples interpolate that same geometry, with distance-based feathering
and seam shading. Contextual interiors use one-third of the world-space warp
(bounded to two pixels of vector displacement), leaving ten pixels for shaping.
A one-pixel band at tile edges retains the existing mask's displaced crossings and feather weights, then
smoothly blends to the new geometry by four pixels. This compatibility band joins
natural curves to existing complex junctions without introducing tile seams.
It retains the existing edge detail rather than shifting every crossing to the
marching-square midpoint. The twelve-pixel displacement limit applies to contextual
interiors; existing masks in compatibility bands and fallback cells keep their
original limits.

Recipes and page-cache source windows include the halo, so painting, undoing, or
rerolling a look refreshes affected neighbouring cells, including across map wraps.
These are transient presentation inputs; terrain storage, saves, pathfinding,
simulation randomness and checksums are unchanged.

### Map seed

Every hash in this chapter takes wrapped coordinates, a material or profile salt
and the map's terrain look seed, `Map::terrainSeed()`. The seed is saved with the
map (format 138), travels with the map file in multiplayer, and reaches the
renderer through `SceneMap::terrainSeed()` and `Recipe::seed`, so composed pages
rebuild when it changes. Generators derive it from the generation request seed
(`GenerationContext::deriveSeed(seed, "terrain-look")`) without consuming the
synchronized stream; the editor's menu entry **Reroll terrain look** draws a
fresh one and marks the map modified; maps saved before format 138 load with
seed 0. Two maps with the same cells therefore look different, while every
client of one map draws it identically. The seed is presentation state: it is
not part of `Map::checkSum()` and no simulation code reads it. Diagnostic calls
such as `coverage()` default to seed 0. The resolver prepares the
control points once per tile; native and HD samples use the same geometry.

A boundary profile has `key`, `roughness_q8` (0–512, where 256 is a multiplier of 1),
and `contours_q12`: four displacement curves, or four to sixty-four in version 3.
Each has 5, 9, 17 or 33 evenly spaced points, starts and ends at zero, and uses
integer displacements within −512…512 (−1024…1024 in version 3) in normalized
units of 1/4096 of a lattice patch, which equal Q8 pixels. A shared-edge or
patch hash picks the curve and also reads it mirrored or negated, so a profile
with n curves offers 4n edge shapes; more curves mean less visible repetition.
These are displacement controls, not pixel coordinates. The common endpoints
keep neighboring patches joined. Each shared edge has one displaced crossing;
detailed curves shape the patch interior. This prevents steep authored notches
from folding a shared edge into disconnected slivers.
The runtime interpolates these curves in normalized coordinates, so native and HD
renders use the same shape. Local contours displace samples inside their own
patch, bounded by the profile's `amplitude_q8` (0–1024 Q8 pixels, default 512);
only the world-space warp consumes the eight-pixel halo of prepared patches.
Version 3 reads a shear curve as the displacement at the patch center (earlier
versions doubled it) and holds that displacement over the central half of the
patch, which stays fold-free up to the four-pixel limit. Center regions and
narrow roads remain visible. `tools/terrain_profile_curves.py` regenerates the
shipped curves: sand traces the thirty-two straight edges of the original
grass/sand transition tiles (two 17-point patches per edge, endpoint drift
removed), ice uses seeded angular random walks and cobblestone broad nine-point
plateaus. These profiles shape silhouettes; they do not trace individual stones
or cracks in the interior artwork.

Corner weights pass through a smoothstep, so a single-corner region approaches a
quarter disc instead of a chamfer and a lone quadrant renders as a round blob;
shared-edge weights and edge midpoints are unchanged. Where two corners of a
patch hold the same material diagonally, a per-vertex hash picks one of the two
materials to join through a neck whose half-width is that profile's `bridge_q8`
(0–1024 Q8 pixels); the other pair stays separated, as in the original diagonal
tiles. Optional `speckle_q8` (0–1024 Q8 pixels) scatters pebbles of each material
on a four-pixel world grid (half of the cells, 1.25 to 2.75 pixel radius). A
pebble raises its own material's score, so specks and bites appear up to roughly
that many pixels across a boundary, like the detached grains in the original
sand art, while interior samples are untouched. Pebbles are hashed from wrapped
world coordinates and the material key, so they continue across tiles and agree
on both sides of a shared edge.

Optional `feather_q8` controls edge softness from 128 to 512 (half to two native
pixels, default 256). Softness interpolates from material corner profiles, so
adjacent patches agree even where three or four materials meet. Pair treatments
select contour geometry; they do not override this material softness. Keep ice
and cobblestone sharper than sand rather than using a wide fade to hide a regular
outline.

Shared edge keys include canonical wrapped coordinates, orientation and stable
material keys. Both sides choose the same contour. Four corner materials resolve
jointly to normalized coverage, including diagonal, concave and multi-material
junctions. Textures use straight alpha only after coverage and source alpha have
been combined. Feathering is narrow,
not a broad blur over the square boundary.

By default the rougher boundary profile wins, with a stable key tie break. Optional
`pair_treatments` entries contain `a`, `b` and `profile`; these choose another
contour family for that pair without changing material connectivity. Pairs are
unordered, unique and must reference existing materials and profiles. Most
materials need no pair entries.

Textures are kept separate from boundary masks. Runtime preparation blends each
material's variant borders toward one periodic master while keeping interior
texture detail. The four-pixel border blend operates on premultiplied RGBA, so both
color and opacity agree across variants without introducing dark transparent fringes.
The compositor does not bake a texture-by-mask-by-material-pair product.

### Seams

Textures are never cross-faded across a boundary; a wide fade averages unrelated
textures into a smear. Instead the compositor tones a narrow contact band, as the
original tiles did with a dark lip on sand under grass and a wet line along the
shore. The resolver reports, per sample, the nearest other material and an
estimate of the distance to it (the score gap grows by 12288 per Q8 pixel along a
straight edge). Each material's optional `seam` object sets `height` (0–255, a
stacking rank), `cast_q8` (0–256) and `cast_width_q8` (0–2048 Q8 pixels): a
material darkens lower-ranked neighbors by up to `cast_q8`/256 at the contact,
fading to nothing at that width. Equal ranks cast nothing, so sand beside sand
of another variant is untouched. `fringe` (RGB), `fringe_q8` and
`fringe_width_q8` tint any neighbor toward that color, which ice uses for a
faint frost rim on grass. The shipped ranks place water highest so sand and
grass take a wet band at the waterline, then ice, trail and grass, with sand
lowest. Keep casts short
(two to three pixels) and under about a third strength; the goal is a sense of
thickness, not an outline.

## Validate and inspect

```sh
python3 tools/terrain_tileset.py --check
python3 tools/terrain_tileset.py --output artifacts/terrain/compiled --page-size 256
python3 tools/terrain_profile_curves.py --write
python3 -m unittest discover -s tools -p test_terrain_tileset.py
"$(python3 tools/package_assets.py --encoder-python)" tools/artwork/terrain_synth.py --check
"$(python3 tools/package_assets.py --encoder-python)" tools/artwork/validate_material.py --all
python3 test/run_tests.py --filter 'TerrainMaterials/*'
python3 test/run_tests.py --filter 'TerrainPresentation/*'
python3 test/run_tests.py --filter 'TerrainValidation/*'
```

The pipeline has three responsibilities:

1. **Catalog/compiler:** validate the schema and native sources, prepare common
   variant borders, then emit paged texture metadata, contour tables and source
   provenance. Page sizes are powers of two from 64 to 8192; the default is 1024.
   Each 32×32 source has four pixels of padding and three exported mip levels.
2. **Runtime asset exporter:** encode the game's sprite sources, generate the
   terrain pack and audit all output files. It records fingerprints of the actual
   decoded runtime sources, including lossy WebP and sheet frames, separately
   from original PNG provenance hashes.
3. **Runtime compositor:** verify native source fingerprints, read prepared texture
   regions lazily from CPU pages, resolve coverage and compose visible terrain.
   It computes coverage directly from normalized profile controls. Exported
   contour tables and source mips are validation/packaging data; runtime source
   extraction currently reads mip 0. Composed view pages supply their own GPU
   mipmaps and respect the device texture limit.

`compiled_pack` names `atlas.json` in its own data subdirectory, such as
`data/terrain/my-pack/atlas.json`. An absent pack,
an edited catalog, a pack without source fingerprints, or native source pixels
that differ from the recorded runtime export use normal sprite preparation.
This includes user artwork overrides already loaded when the renderer starts.
Present but malformed metadata or missing referenced page images are errors;
regenerate the pack rather than relying on a silent partial load. Optional HD
sources continue through normal HD frame registration and preparation; the native
pack does not replace them. The portable backend loads standalone HD frame sources
and uses the same CPU terrain composition. The optional legacy packed-HD sprite
atlas optimization remains OpenGL-specific.

The standalone command writes a pack for unencoded authoring sources. Normal
release/browser packaging generates fingerprints for that export automatically;
do not transplant a standalone pack into a lossy runtime export. Keep compiler
output under `artifacts/`, away from source assets. To inspect a changed catalog,
validate and rebuild the runtime assets, then restart the renderer. In-process
source surface revisions invalidate cached pixels; catalog file edits do not
trigger a live catalog reload.

Review the gallery at normal play scale and enlarged detail: isolated cells,
one-cell roads, bends, holes, mixed junctions, grass/sand/water corner mixes, torus
edges, translucent water borders and fractional zoom. The current gallery case
also writes a 256-tick simulation checksum trace and cold/warm cache timings.
Compare that trace with the same fixture built against the base revision.

## Caches and compatibility

Saved maps hold terrain IDs per vertex, not sprite frames, so changing visual
variants in the catalog cannot change saved state, checksums or simulation RNG use.
Files older than format 146 also stored sprite frames; the loader skips them, and
only `LegacyTerrainFrames.h` still decodes classic frames, for old script memories.

`TerrainVisual::Compositor` owns prepared material sources. Source lifetime and
content revisions, animation phase and native/HD selection invalidate prepared
pixels. View caches hold 16×16-cell composed pages and compare recipes including
the surrounding lattice, so edits update neighboring tiles and wrapped chunks.
Each page and tile tracks revisions only for materials used by its discovered
recipes; an animation outside that dependency set does not rebuild the page.
Sampling density is part of the page key. Old densities retain their composed
CPU pixels inside the cache budget. Renderer allocations also remain reusable
until memory pressure retires them. Returning to a density revalidates terrain,
discovery and material revisions before reusing its pixels.
The historical `SoftwareTerrainCache` name is retained for benchmark controls,
but the cache also draws GPU pages. Software page storage stays bounded by
32 MiB. GPU density selection retains its 128 MiB allowance, and resident
textures remain bounded by 128 MiB of conservative texture/mip reservations.
On desktop, the total GPU-page cache has a 256 MiB ceiling including CPU pixels,
bookkeeping and resident texture reservations. The additional 128 MiB lets
inactive zoom densities retain their CPU pixels without sacrificing sampling
quality. Android and browser builds retain the 128 MiB total ceiling.
These are maximum memory allowances, not up-front allocations.
Generated/edited pages keep alpha-weighted mip filtering but upload uncompressed
textures. Driver-side DXT encoding is reserved for artwork with prepared mip
chains; repeating that encoding on animated terrain updates stalls rendering.
Crossfades update material revisions on each visual tick, so pages containing
animated materials recompose at that cadence. Repeating the same animation time
reuses their pixels. Pages also keep the coverage of mixed cells that touch an animated material (`Compositor::CellMask`, 10 bytes per
composed pixel), so a phase change re-blends their textures instead of
re-sampling the boundary; coverage depends only on the cell, never on the phase.
These masks have their own budget (8 MiB software, 32 MiB GPU, or 128 MiB for
reduced desktop GPU pages that cover many animated cells); when it is full,
masks of pages not drawn in the current frame are released oldest first, and cells that still do not fit compose without one.
Composition always blends through the same mask encoding, so a kept mask and a
fresh one give identical pixels. HD oversampling falls from 4× to 2× or
1× when necessary to fit the visible pages or the device texture limit. If native
pages still exceed the budget in a zoomed-out GPU view, the cache reduces them by
powers of two as needed, going no coarser than the nearest level to the display's
physical pixel density (at most √2 magnification). Reduction averages composed
native pixels with alpha-weighted colors, so undiscovered (transparent) cells
do not darken their neighbours. A valid retained native page supplies these
pixels directly; reduction does not sample its terrain boundaries again.
This keeps terrain reusable during the detailed-to-overview crossfade, instead of
recomposing the entire visible map every frame. The reduced detail can soften
texture grain at distant zooms. Software pages retain native density. Prepared
source pixels are reported separately by `sourceBytes()`.

Density selection uses map zoom multiplied by the active target raster scale,
including HiDPI windows and explicit offscreen capture scales. This preserves
output detail independently of the window hosting a capture. It is chosen for the complete
view and shared by cached and streamed pages; tiled map captures also share the
whole capture's density at narrow edges. A torus capture additionally reduces
its atlas and terrain pages until the complete map's CPU pages and renderer
reservations fit their budgets together. This avoids evicting earlier capture
tiles while drawing later ones. Small maps keep native atlas density; large maps
can soften fine detail when magnified. The atlas uses the device's texture and
viewport limits before splitting into tiles. The budget includes a fixed allowance
for recipes and bookkeeping plus density-dependent pixel storage. Increasing it
can retain more detail but does not remove the need to handle oversized views.
If the complete view cannot retain all of its textures simultaneously, but its
CPU pages and one texture upload fit, the cache keeps those CPU pages and retires
textures in drawing order as needed. This preserves the selected sampling and
avoids repeated boundary composition near page-alignment budget thresholds.
Only a view whose CPU pages and one upload cannot fit uses composition streaming.
First-time composition still evaluates native terrain before reduction; this
policy removes repeated work on warm frames, not the cost of a cold frame.

Overview palette samples have a separate 32 MiB CPU page cache. Its pages keep
canonical world coordinates across viewport movement, atlas tiles and zooms.
Vertex windows include the contextual contour halo; terrain seeds, immutable
terrain/resource registries and map asset bundles also invalidate the samples.
Resource tint is refreshed when its type or discovery changes. Texture animation
and blend opacity do not change the overview palette. The viewport scratch image
continues to use the original 4×4 samples per cell and rendering path.

The `TerrainPresentation` tests cover zoomed-out cache admission and warm reuse,
wrapped views, alpha-weighted coast reduction, terrain-edit invalidation,
cached/streamed pixel equivalence, restored close-up detail and HiDPI limits.
The zoomed-out case records cold/warm timings and images. Timings are terrain-only
diagnostics, not whole-game frame rates. After building `engine-tests`, run:

```sh
python3 test/run_tests.py --binary engine --filter 'TerrainPresentation/*' \
  --filter 'TerrainValidation/*' -j2 --artifacts artifacts/terrain-cache
```

When the full view cannot stay cached, rendering streams one temporary canonical
page at a time. It uses the same sampling density, opaque runs and mip neighborhoods
as the cached view, including fractional zoom and maps smaller than one page.
If even one page cannot be allocated or fit the device limit, rendering falls back
to a reusable composed tile. This emergency path preserves coverage and deterministic
artwork, but fractional resampling and HD mip filtering can differ from page rendering.
It is a quality fallback, not a pixel-identical cache replacement.

Minimaps and thumbnails read the catalog's compact palette for built-ins and the
embedded resolved colors for runtime types, without changing legacy thumbnail
decoding. Map image interchange colors and editor experiment gates remain semantic
metadata.
Keep reference screenshots, benchmark output and temporary compiled tilesets under
`artifacts/`; publish review evidence separately from durable documentation.

## Map-owned custom artwork

The online set workspace and editor import use the same terrain material parser
and compositor, with sprite paths resolved from the immutable map bundle rather
than the global Toolkit cache. Custom materials bind by stable terrain key, can
use the installed boundary profiles and have independent decor sheets. Built-in
artwork stays installed; it is never copied into a map bundle. See
[themed sets](../features/resource-catalogs.md#themed-terrain-and-resource-sets) for
authoring, frame bounds, attribution and offline sharing.

Sheets are identified by their PNG content hash. When combining sets, identical
PNG bytes must use the same frame width and height; conflicting frame grids are
rejected without changing the map. Use a distinct sheet image when the same art
needs a different grid. Missing bundled sheets are validation errors. Older
manually imported definitions that reference installed artwork retain their
ordinary missing-art fallback.
