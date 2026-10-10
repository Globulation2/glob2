# Terrain production

Companion to [terrain materials](terrain-materials.md).

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
that cadence; see [Caches and compatibility](terrain-rendering.md#caches-and-compatibility) for the
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
[Seams](terrain-rendering.md#seams) requires. Equal ranks cast nothing, so barren ground beside grass
takes no lip. A lighter inner lip inside a hole cannot be expressed by the seam
model, which only tones neighbours; it would need an engine-side self-lip.

Related: [asset production](README.md).
