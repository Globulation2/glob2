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
- optional `backdrop` with `sprite`, `first_frame`, `frames`, and `ticks`;
- optional `seam` (version 3) with `height`, `cast_q8`, `cast_width_q8`, `fringe`,
  `fringe_q8` and `fringe_width_q8`, see [Seams](#seams);
- `ocean: true` only for materials that reveal the shared scrolling ocean.

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
than one phase. Every effective frame must exist and remain below 65536. Backdrops
have their own consecutive `first_frame`, `frames` and `ticks` fields and are
composited beneath the material before border preparation. The water binding must
use `ocean: true`; ocean materials reveal the shared scrolling ocean instead of
using a per-material backdrop.

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
luma targets by one affine transform computed from variant 0, so all variants of
a material share one tone. Variant 0 also supplies the border band: every other
variant's outer pixels are blended toward variant 0's render before downsampling,
and stamped features that could touch that band (boulders, twigs, flowers,
embers) are drawn from a stream shared by all variants, so any two variants join
without cut or ghosted features and the runtime's own four-pixel blend toward
variant 0 is a no-op. `share_perimeter` then copies ring 0 from tile 0 (opposite
edges read the same coordinates, so a tile also joins itself across the torus
seam) and blends ring 1. Group readability is part of the recipes: obstacles are
dark and shadowed, hazards warm and saturated, fertile ground dark saturated
green, barren ground muted warm neutrals, rough ground cool and desaturated,
paths warm and light, void near black.

Lava and ember field are animated: four phases per variant, frame
`variant + 16 * phase` (frames 0–63), catalogued with `animation_frames 4`,
`animation_stride 16` and `animation_ticks 8`. The crust layout is shared by the
phases; only the glow ramp moves.

Deep water and dark water are translucent RGBA tints (`ocean: false`, alpha
about 185 and 230). Ocean materials have no texture of their own and every
non-ocean material composites with straight alpha over the shared scrolling
ocean, so a tint darkens that ocean and is animated for free; a per-material
`backdrop` would need its own 32×32 frames and was rejected for that reason.
Marsh pools use the same mechanism at alpha 215.

`--check` re-synthesises every material and compares the pixel hashes with the
committed PNGs and with `provenance.json`; it also requires the recorded
generator hashes and Pillow 12.2.0, so any edit to `terrain_synth.py` or
`material_tiles.py` is followed by a default run that refreshes the provenance
(pixels that did not change produce byte-identical PNGs). Recipes swapped to
image-generated art are marked `placeholder_only=True` and skipped by `--check`.
`--hd` writes the 128×128 renders under `artifacts/terrain/hd/` for review only.
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
supersedes). For lava and ember field, `--animate-glow` keeps the generated crust
and channel layout and applies the recipe's glow ramp to the warm pixels for four
phases (`method: "hybrid"`). Then mark the recipe `placeholder_only=True` in
`terrain_synth.py` and run `validate_material.py --name <name>`.

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
| `rock` | 384 / 896 / 192 / 448 / 192 | 16 × 9, angular | boulders, ridge_rock, outcrop, scree, gravel, chasm |
| `soft` | 160 / 512 / 448 / 128 / 640 | 12 × 17, gentle | mud, marsh, loam, moss, deep_snow, deep_water, dark_water, clay, dirt |
| `crisp` | 96 / 256 / 128 / 0 / 256 | 8 × 9 | void_hole, boardwalk |
| `brush` | 320 / 768 / 320 / 384 / 512 | 16 × 17 | hedge, thicket, flower_meadow, spring_meadow |
| `sand` (existing) | | | dirt_track |
| `fractured` (existing) | | | lava, ember_field |

`tools/terrain_profile_curves.py --write` generates the curves with seeded random
walks and appends a missing profile with these parameters; never author curves by
hand. Pair treatments: boulders, ridge_rock and outcrop against grass use `rock`;
hedge and thicket against grass use `brush`; water/deep_water and
deep_water/dark_water use `soft`; lava and ember_field against grass, sand, dirt
and gravel use `fractured`; void_hole and chasm against every other material use
`crisp` (the default rule would otherwise let the rougher neighbour win).

### Seam ranks

| Rank | Materials | Cast (`cast_q8` / `cast_width_q8`) and fringe |
| --- | --- | --- |
| 7 | dark_water | 80 / 704 |
| 6 | deep_water | 72 / 640, a drop-off lip on shallow water and shores |
| 5 | water (existing) | 72 / 640 |
| 4 | boulders, hedge, thicket | 80 / 768, a sense of height |
| 4 | ridge_rock, outcrop | 72 / 768 |
| 4 | lava, ember_field | no cast; warm fringe `[214,110,40]` 96/512 and `[160,80,40]` 64/384 |
| 4 | ice (existing) | 64 / 640 with frost fringe |
| 3 | road (existing), dirt_track, boardwalk | 48 / 512 |
| 2 | grass (existing), loam, moss, spring_meadow, flower_meadow | 56 / 512 |
| 2 | dirt, clay, gravel | 40 / 448 |
| 2 | deep_snow | 32 / 448 |
| 1 | sand (existing), mud, marsh, scree | no cast |
| 0 | void_hole, chasm | no cast; rim fringe `[54,50,66]` 80/384 and `[44,34,40]` 64/384 |

Casts stay within two to three pixels and under a third strength, as
[Seams](#seams) requires. Equal ranks cast nothing, so barren ground beside grass
takes no lip while every neighbour casts into a hole.

## Boundaries and masks

The presentation resolver uses a 16-pixel lattice. Whole-cell terrains fill their
four quadrants; legacy sprites decode into their original TL/TR/BL/BR material
configuration, including the reversed diagonal groups in the sand/water atlas.
A test verifies both profiles against the engine's frozen lookup. The resolver
reads the scene snapshot, never the live simulation. `PreparedCoverage` resolves
the nine patches needed by a tile once, including shared contour choices and
side-connected corner groups, then samples them at native or HD pixel centers.

The optional catalog-level `boundary_warp_q8` array controls world-space bends at
64-, 32- and 8-pixel scales. The shipped values `[640, 256, 96]` allow about four
pixels of combined displacement per axis: a broad meander, a medium ripple and a
faint angular grit. The first two scales interpolate smoothly; the finest adds
angular irregularity. Pebbly detail comes from the authored profiles below. These bends continue across tile
boundaries instead of restarting a motif in every patch. Values are nonnegative
integers, bounded by `[1024, 384, 128]`; omitting the array disables the field
for older packs. Try reducing the first value for straighter edges, or the last
for less fine detail.

All materials share this field so multi-material junctions remain joined. Wrapped
world coordinates determine its control points, independently of texture variants,
camera position, animation and simulation randomness.

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
junctions. Ocean coverage becomes transparency; other textures use straight alpha
only after coverage and source alpha have been combined. Feathering is narrow,
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
lowest. Ocean pixels are transparent and receive nothing. Keep casts short
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
one-cell roads, bends, holes, mixed junctions, legacy shore orientation, torus
edges, translucent water borders and fractional zoom. The current gallery case
also writes a 256-tick simulation checksum trace and cold/warm cache timings.
Compare that trace with the same fixture built against the base revision.

## Caches and compatibility

`TerrainCompatibility.h` freezes old frame ranges, corner semantics and the frame
hash used by map authoring. Legacy lookup keeps its synchronized random calls.
Changing visual variants in the catalog cannot change those contracts.

`TerrainVisual::Compositor` owns prepared material sources. Source lifetime and
content revisions, animation phase and native/HD selection invalidate prepared
pixels. View caches hold 16×16-cell composed pages and compare recipes including
the surrounding lattice, so edits update neighboring tiles and wrapped chunks.
Each page and tile tracks revisions only for materials used by its discovered
recipes; an animation outside that dependency set does not rebuild the page.
Changes to the overall page sampling density still invalidate view pages.
The historical `SoftwareTerrainCache` name is retained for benchmark controls,
but the cache also draws GPU pages. Software storage stays bounded by 32 MiB;
GPU pages have a separate 128 MiB budget. HD oversampling falls from 4× to 2× or
1× when necessary to fit the visible pages or the device texture limit. If native
pages still exceed the budget in a zoomed-out GPU view, the cache reduces them by
powers of two as needed, going no coarser than the nearest level to the display's
physical pixel density (at most √2 magnification). Reduction averages composed
native pixels with alpha-weighted colors, preserving fractional coast coverage
without darkening edges against the ocean.
This keeps terrain reusable during the detailed-to-overview crossfade, instead of
recomposing the entire visible map every frame. The reduced detail can soften
texture grain at distant zooms. Software pages retain native density. Prepared
source pixels are reported separately by `sourceBytes()`.

Density selection uses map zoom multiplied by the active target raster scale,
including HiDPI windows and explicit offscreen capture scales. This preserves
output detail independently of the window hosting a capture. It is chosen for the complete
view and shared by cached and streamed pages; tiled map captures also share the
whole capture's density at narrow edges. The budget includes a fixed allowance
for recipes and bookkeeping plus density-dependent pixel storage. Increasing it
can retain more detail but does not remove the need to handle oversized views.
First-time composition still evaluates native terrain before reduction; this
policy removes repeated work on warm frames, not the cost of a cold frame.

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
