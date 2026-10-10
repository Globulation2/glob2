# Terrain rendering

Companion to [terrain materials](terrain-materials.md).

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

Related: [asset production](README.md).
