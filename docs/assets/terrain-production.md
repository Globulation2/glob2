# Terrain production

Companion to [terrain materials](terrain-materials.md).

## Material production

All 55 built-in materials, including grass, sand, their lush, dry, dune, pale and
windy dune looks, road, ice and regular water,
are produced by `tools/artwork/terrain_synth.py`. Each material is a small
pack that can be tuned on its own: its recipe and render function live in
`tools/artwork/terrain_packs/<name>.py` and its catalog block in
`content/data/terrain/materials/<name>.json`. Materials of one family share a
helper module there (`_waves`, `_sand`, `_grass`, `_heather`), and
`tools/artwork/terrain_core.py` holds the shared field library, the group-mark
helpers and the recipe registry. A material's provenance records the pipeline,
the shared library, its own module and the family helpers it imports, so tuning
one material changes only that material's record. To add a material, add a
module there; the package registers every module it contains. Static materials have four
interchangeable 8×8 layouts (256 variants), and so do lava, ember field and molten lava pool, so
their glow no longer repeats on the 8×8 grid (1,024 frames each over four
phases); water and deep water have one 4×4 layout, keeping their animated
banks at 256 frames each. Frames are stored as
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
"$ENC" tools/artwork/terrain_synth.py                       # write all procedural frames and their catalog files
"$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
"$ENC" tools/artwork/terrain_synth.py --emit-catalog        # material blocks and colour initialisers
"$ENC" tools/artwork/terrain_synth.py --write-catalog       # refresh the catalog from committed frames (no render)
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
`grid=8, blocks=4`, as do lava, ember field and molten lava pool, and the waters `grid=4`:
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
helper (in `terrain_core.py`, or a family module under `terrain_packs/`), while
colour and material stay free per biome:

| Group | Mark | Helper |
| --- | --- | --- |
| Grass | baseline, no mark | |
| Fertile (loam, moss, spring meadow) | warm, saturated ground | |
| Water, deep water, lava (lava, ember field, molten lava pool) | reserved colour | |
| Snow and ice (ice, deep snow, wind-swept snow, frozen lake, glacier ice) | reserved cold white, pale teal and glacier blue | `warped_edges`, `bubbles` (frozen lake), `drift_ridges` (wind-swept snow), `fracture_planes`, `flat_bubbles` (glacier ice) |
| Sand | warm fine grain | `sand_look` |
| Barren (dirt, clay, shale, heath, weathered sandstone, tundra, volcanic ash, laterite, arctic tundra) | firm, level, packed ground: faded colour, relief two to twelve native pixels across damped, a fine compaction grain, evenly strewn grit and small flat stones with short shadows, and a few faint hairline cracks; contrast targets about 0.7 of other grounds' so it reads quiet and buildable rather than broken | `barren_mark` (`level_ground`, `compaction_grain`, `group_gravel`, `group_cracks`); `plates`, `jag`, `cast` (shale) |
| Paths (road, brick path, boardwalk, sandstone tiles, mossy setts, slate ashlar, basalt causeway, scoria blocks, rime ashlar, moonstone lozenges, frost pinwheel) | laid, made surfaces: joints between setts, bricks, planks or flags, with a crisp, lighter worn lip, worn into the ground | `worn_edge`, `_paving` (settle, chip, crack, lose, fill and reclaim), `herringbone`, `hex_paving` (mossy setts); `_rock_paths` (ash and upland joint ground, lichen and grain stamps); `_frost_paths` (snow in place of soil and moss, `weather`) |
| Rough (mud, mudflat, scree, pumice field, hardpan) | heavy relief about two native tiles across: lumps, curled plates or heaped slabs, with sunken pits | `lumps`, `wet_pits`; `rock_layer`, `scree_fields` (scree) |
| Crags (`ridge_rock`, shown as Rock spires; `outcrop`, shown as Basalt columns) | long low ledges: a lit lip, then a short shaded riser | `ridge_mark` |
| Void (hole, chasm) | near-black depth; the hole casts onto every neighbour | |
| Obstacles (boulders, hedge, thicket, cliffs, glacier) | tall raised decor | `terrain_decor.py` |

Within the groups: dirt keeps the original speckled earth and stones under the
shared mark; clay is red baked clay in bold plates; shale is big flat plates of grey stone lying over one another;
volcanic ash is dark charcoal fine-grained ash with scattered porous black stones and
rusty mineral specks;
weathered sandstone is a solid bed of ochre stone worn into large, low, rounded slabs with
fine joints, rust stain and pale nodules; laterite is a packed bed of rounded, hardened
iron-red nodules (a few glossy brown-black iron) over a red crust whose softer cracks run
between them, with lower patches of bare ochre fines; heath is clumps of dormant and dead heather cushions,
each built of lit sprig tufts, over pale packed sandy peat; tundra is its cold
counterpart on frost-patterned ground: polygons about two cells across with faint
pale rims, flat olive moss pads (the heath cushion helper) in their centres, beds of
grey stones, and the shared mark in dark peaty earth. Arctic tundra is tundra
under snow: the same polygons, stones and packed ground, with snow drifts in place of the
moss pads (the same cushion helper and placement, merged into scalloped drifts), rime
on the rims, a dusting on the stones and the faint cracks running on through the snow
as blue fissures. Paths are all laid by the
globs, never natural ground, and all of them have been walked for years: the
`_paving` family module settles every stone with its own tilt, sink or heave
(half of it following broad subsidence hollows, so neighbours sink together)
and its own bevel width, chips the rims, cracks a few stones, loses a few to
the soil (ten times as many in reclaimed patches of a broad growth field),
fills the joints with soil, grit and moss, lets moss creep over the stone in
reclaimed patches, grows weeds from lost stones, blotches lichen, casts the
raised stones' shadows, draws the group's worn lip and dulls everything with a
dust wash. Each path keeps its own lattice, colour and wear language so the
four stay apart at 32 px: road (the trail) is cool grey setts in running
courses of uneven height, lost in clusters to dark soil; brick path is dark
terracotta pavers, two by one and half a tile long, in an axis-aligned
herringbone that reads the same whichever way the path runs, set in pale sand
mortar that is lost to soil in places, with a few spalled dark bricks;
sandstone tiles are square flags, two by two per cell with some cut as double
flags, in two families (warm ochre and cool grey-tan) with joints of varying
width, split by wider cracks and marked by lichen; boardwalk is warm planks in
runs of uneven width, silvered where the weather had them longest, cupped and
tilted, split along the grain, rotting dark at damp ends or right through,
with a few rotted away from one end over the soil. Mossy setts (material key
`overgrown_road`, renamed once the other paths were worn too) is hexagonal
stone setts
about twelve native pixels across in dark joints, decaying: moss along part of
the joints, a few cracked setts, and reclaimed patches where setts are lost to
soil, olive moss and weeds and moss creeps over the stone edges. Its hex lattice
is squashed by under one percent so it repeats exactly on the block, and each
sett's tone, tilt and fate come from the shared stream when it reaches the
block edge. The three rock paths sit beside the grey and volcanic grounds and
each has its own wear language over `_paving`'s; `_rock_paths` gives them their
joint ground and stamps. Slate ashlar is blue-grey slabs of a few modular sizes,
a quarter tile to a tile long, in random ashlar with dark grit joints: the grain
runs along each slab's long side, rims flake away in thin stepped layers that
show darker fresh stone, a few slabs have lost a whole top lamina or split right
across along the grain with one side settled lower, crusty lichen rosettes hug
the rims of quiet stretches, and lost slabs leave grit and grey moss. Basalt
causeway is near-black polygonal stones about half a tile across, five to seven
sided and evened out by one Lloyd relaxation step so they read as laid rather
than as a honeycomb, in pale ash joints so the dark stone stays a road beside
volcanic ash and pumice: walked stretches polish the crowns, stones tip and
sink unevenly, about one in six has shattered along a finer web of cooling
joints, and ash drifts fill the joints and settle over sunken stones. Scoria
blocks is dressed porous lava stone, a tile by half a tile, in basketweave
offset half a square so no joint follows a tile edge: rims are bitten back and
shed cinders into the joints, walked stretches dish the faces, vesicles come in
clusters, some packed with ash, a few oxidised blocks are brown and bleed rust
into their joints, and lost blocks are often only partly gone, leaving a pit of
loose cinder. The three ice paths sit beside deep snow, wind-swept snow and the
ice grounds of a frozen cave. `_frost_paths` swaps `_paving`'s soil, moss and
weeds for the periwinkle of deep snow, so reclaimed stretches read as the
surrounding snow drifting back over the road, and its `weather` adds wind-blown
streaks settling in joints and hollows, powder feathering out of each drift,
refrozen glaze on damp stretches, and sparse warm sanding grit on walked
stretches, the cue that keeps a pale road apart from the slow, cyan ice.
Rime ashlar is blocks cut from the glacier in uneven courses, each its own depth
of blue, milky toward the rim and crossed by pale fracture planes. Moonstone
lozenges are white marble diamonds a tile across on a 45 degree lattice, veined
grey-blue flag by flag, with blue moonstone keystones where four meet, about
half of them frosted over. Frost pinwheel is frosted quartz pavers turning
around small ice squares, offset so no joint follows a tile edge, with sparse
crystal glints and about half the squares frosted opaque. Mud is churned lumps about a fifth of a tile across and
water-filled pits;
mudflat is desiccated mud in big curled plates split by deep cracks,
with salt bloom and a few damp sunken plates; scree is a bed of loose, angular,
weathered cool-grey stone from chips to slabs about half a tile across, heaped so
they rest on and lap over each other over ochre fines, each stone its own mottled
tone, softly lit with shaded gaps between and lichen on a few upper faces, on a
bed that rises and falls about a tile across so lit heaps and shadowed hollows
carry the group's relief; a step lighter and cooler than the ochre talus under
the cliffs;
pumice field is dark porous
volcanic rock in blocks about a third of a tile across, riddled with vesicles,
beside lava and ember field; hardpan is pale
baked crust in small, even, low pillows split by thin cracks, dimpled at their
centres, with a few shallow damp hollows. Molten lava pool plays exactly like
lava, but brighter and hotter: thin red-hot crust plates about a cell across
float on a molten pool, split by a few wide yellow trunk channels that taper
into branches and thin seams, with an orange halo bleeding into each plate's
rim, connected orange secondary cracks and lumpy crust; each plate's channels
and halo pulse on their own clock. Frozen lake
plays exactly like ice: frosted teal lake ice with a few long white pressure
cracks, trapped bubbles and frost patches, static and paler than any water.
Glacier ice also plays exactly like ice: bright cyan-blue depth in calm plates,
crossed by a web of short kinked white cracks, a little over one per tile and
each under two tiles long, that throw a feathered glow to one side, with small
flattened bubbles and fine rime.
Wind-swept snow plays exactly like deep snow: one prevailing wind carves it into
drifts a little under half a tile apart, each a long lit windward slope rising to
a crisp bright crest over a short lavender lee shadow, with quiet flats and faint
ripples between. Its ridges are a plane wave with whole wave numbers on the block, so
only the smooth warps and drift heights vary between layouts. The helpers keep the block contract: geometry comes from
the shared stream at the block edge, interiors vary, and any normalisation or
lighting uses the common reference. Obstacles are lit from the top left and cast
onto grass, and void casts onto every neighbour. Feature sizes are judged
against the globs, not against a photograph: a tile is about a glob across, so
a brick, a cobble, a sand ripple or a crack in the ice should be the size it
would be beside a glob standing on it, and nothing but broad tone should span
several tiles. The cracks of glacier ice, the drifts of wind-swept snow, the
ripples of windy sand, the pavers of brick path, the stones of scree and the
blocks of pumice and mud were all brought down to that scale. The grass and sand
looks keep the grain of the classic tile they stand in for and change only its
colour: lush and dry grass are soft like grass, dune and pale sand speckled like
sand. Windy sand dune keeps that grain and sweeps it into wind ripples about a
quarter of a tile apart
(`ripple_light`): long lit windward slopes, a bright crest and a short shaded lee,
with crests square to the water's main swell (`RIPPLE_WIND` matches `WAVE_SWELL`)
and a second ripple family that breaks them into forks and ends. All four sands use lit grains one or two native pixels across, half the
size of the original sand tile's grains. Like grass and sand they are four-layout 8×8 block
materials, and they take the same pair treatments (`GRASS_LOOKS` and `SAND_LOOKS`
in the synthesiser).

### Raised obstacle decor

Obstacle terrain is drawn in two layers, as forest is. The ground material is
composed like any other terrain: dusty earth for boulders, leaf litter for hedge,
undergrowth for thicket, talus for cliffs, moraine on snow for glacier, and rock for ridge rock and outcrop. A decor sprite per
cell is then drawn with the resources, row by row, so objects stand up, overlap
neighbouring cells and occlude each other.

A material opts in with a `decor` block:

```json
"decor": {"sprite": "content/data/gfx/terrain-decor", "full": [0, 1, 2], "edge": [8, 9]}
```

- A cell draws up to three decor layers, in order: connected decor that
  stands on open ground (`"connected_open": true`, cliffs and glacier),
  connected foliage (hedge and thicket), then every other obstacle (boulders
  and crags). Each layer draws only from its own corners, so cliffs show their walls where they
  meet foliage or crags, and the foliage or rocks keep their own edge on the
  other side. Eligibility follows rendered material bindings, including custom
  materials.
- An undecorated corner, such as ordinary grass, sand or water, suppresses the
  foliage and obstacle layers in that cell, preserving a clear border against
  open ground. The open-ground layer still draws there, so the rim and walls of
  cliffs and glacier stand on the grass.
- Obstacle clusters stand in the middle of their cell, not on their corners,
  so a cell where cliffs hold more corners than the obstacles draws only the
  cliff edge; otherwise a stray rock would stand on the plateau.
- Uniform cells keep their existing `full` or connected mask-15 frames and
  interior variants. Within one layer, mixed materials draw just one
  material's ornament: a deterministic coordinate/terrain-seed hash chooses a
  corner, so two-plus-two corners give each material equal odds and three-plus-one
  corners give a three-to-one split. This uses no simulation random numbers.
- Mixed decorated cells use the selected material's `edge` frames, including
  when it occupies just one corner. Connected decor retains its authored
  corner geometry; connected materials of the same layer join into one crown of
  the selected material (hedge with thicket), while other layers' corners count
  as an edge. No overlapping ornaments or new artwork are needed.
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
- Frames are at most 128×128 (`TerrainVisual::MaximumDecorFrame`). Frames up to
  64 pixels tall are centred on the 32×32 cell; a taller frame keeps a 64×64
  frame's bottom edge, 16 pixels below the cell, and grows upward, so tall
  obstacles stand on their own cell instead of spilling into the row to the
  south. Every frame is centred horizontally (`decorOffsetX`/`decorOffsetY` in
  `TerrainMaterials.h`; the map, editor swatches and the review sheets share the
  rule). The resource pass draws the rows and columns just outside the view
  whose decor reaches into it, from the catalog's largest frame, so tall decor
  does not pop in at screen edges. Connected foliage uses cropped 32×64 frames
  to limit texture memory. Object silhouettes may overhang neighbours;
  connected canopy surfaces join at cell boundaries.
- Units and buildings draw after all decor, so a unit standing just north of a
  tall boulder draws over the boulder's top.

`tools/artwork/terrain_decor.py` synthesises the frames in the game's soft
three-quarter view:

- Each object stands on a base point and rises into the cell above.
- The body has a lit top and a darker front face, with edge shading only on the
  side away from the light.
- One soft ground shadow falls to the lower right.
- Boulders are tall: 128×128 frames whose stones are a faceted, lit top on a
  front face extruded down to the ground, flaring slightly toward it. The
  largest stand most of a cell above their own. Each stone's footprint, swept
  along a low sun to the lower right, gives a long shadow that fades with
  distance and lies on the ground under every stone.

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
Hedge rises sixteen native pixels above its ground material and thicket
fourteen; both sample one continuous canopy texture across a 4×4 block of
cells. Their front wall shows the foliage just behind the rim above it, and the
raised mass casts a long shadow straight down the screen into the 16 pixels
below its cell, fading to nothing; where the canopy continues, the next row's
frame covers it. Thicket has broader
boundary irregularity and woody gaps between crowns. The other sets are faceted boulders and rubble, clusters of faceted crags along
a shared grain, and broad stepped bedrock blocks with sparse lichen. Ridge and
outcrop rocks are flat-shaded prisms: a convex ring raised into walls, with
sloping top facets meeting at an off-centre peak, so each face takes one light
value.

Cliffs use the same connected corner tiles, but as rock: a weathered ochre
plateau standing twelve native pixels above its footprint, twice the rise of
foliage, in 32×64 frames. Its relief is broad shelves with fine grain, pits,
lichen stains, loose rocks and a few jointed fractures, under broad light that
rises toward the rims facing the light. Each south face is the plateau's own
stone, stretched down the drop and put in shade: broad facets each turned their
own way to the light, faint broken bedding, darkest at the foot, over a dark
contact strip and a long shadow. The lip is lit in broken stretches; rims facing
away from the light fall dark. The plateau has no discrete objects that could
repeat. Each vertex colour owns a patch of noise fields (relief, tone and grain)
that a cell blends with variance-preserving bilinear weights, so along a shared
edge only that edge's two vertices count, and filled cells add a seeded interior
patch whose weight is zero at the cell edge. Rocks belong to vertex discs and
fractures to cell edges (keyed by both end colours) or interiors, each drawn only
by the frames that share it. Its ground material is talus: the chips and cobbles
of scree in the cliff stone.
Cliffs set `connected_open`, so their partial-corner tiles draw against open
ground and even a one-vertex ridge stands up with its walls; the plateau's rim
follows the crags rather than the cell grid. Cliffs draw in their own decor
layer, so against hedge, thicket or crags they keep their walls and the
neighbour keeps its own edge.

Glacier is the same connected plateau rendered in ice by a second cliff style
(`GLACIER_STYLE` in `terrain_decor.py`): a snowfield of firn with wind-packed
drifts and smoother, low shelves, split by transverse crevasses that open on
blue depths, over walls of blue ice that deepen toward the foot, crossed by
broken debris bands under a white snow cornice at the lip, with a blue cast
shadow. A cell edge has only four variants, which would line its crevasses up
along rows, so glacier crevasses belong to the hash-chosen interiors of filled
cells, at most two per cell on separate rows. Its ground material is moraine:
the talus chips and cobbles, thinned out, in cold grey stone on packed snow.
Glacier sets `connected_open` like cliffs and draws in the same decor layer,
so it joins cliffs without a wall between them; against hedge, thicket or
crags it keeps its walls, as cliffs do.

The tool writes `content/data/gfx/terrain-decorN.png`
(legacy set k at frames 12k–12k+11; hedge corner tiles at 60–315 and interiors at
316–427; thicket corner tiles at 428–683 and interiors at 684–795; then each later
set appends its twelve object frames and its connected frames, so cliffs take
796–807 and 808–1175 and glacier 1176–1187 and 1188–1555), the HD frames, provenance and, with
`--write-catalog`, the `decor` blocks. `--sheet` writes a review sheet. Select `--decor hedge --decor thicket --sheet <path>`
for before/after fields with narrow strips, corners, isolated vegetation and a
hole. Decor is presentation only: the minimap, the overview and
the simulation ignore it.

### Ground overlays: fertile glints

Each terrain group carries one shared mark, and fertile ground (loam, moss,
spring meadow) twinkles. The glints are a flat animated overlay, not an animated
ground texture: fertile land covers large areas, and an overlay is drawn as
sprites every frame without invalidating cached terrain pages. A material opts in
with an `overlay` block:

```json
"overlay": {"sprite": "data/gfx/terrain-glint", "cycles": [[0, 1, 2, 3, -1, -1, -1, -1]],
            "ticks": 5, "density_q8": 64}
```

- Only `density_q8`/256 of the cells draw an overlay at all, chosen by a
  presentation hash of the cell, the material and the map's terrain seed.
- A chosen cell keeps one of the `cycles` and starts it at its own hashed step,
  so neighbouring glints never flash together. It advances one step every
  `ticks` steps of the presentation clock that animates lava and water
  (`MapRenderState::animationTime`, one per drawn frame), so overlays pause with
  the game and still twinkle in the map editor, where the simulation tick never
  moves. A step of `-1` draws nothing.
- A cell draws an overlay only when all four corners resolve to materials with
  one, so glints stay on their ground; a mixed cell picks one corner's material.
- Overlays lie flat: the renderer draws every visible row's overlays before any
  decor or resources, so raised decor reaching down from the row above still
  covers them. Frames may be up to 64×64 and are centred on the cell, like decor.
  All overlay blocks share one sprite. Editor swatches show one lit step per cell.
- Timing and density can be tuned in the material files without regenerating frames.

`tools/artwork/terrain_glints.py` draws one mote per layout at four brightness
levels (rising, a four-pointed peak star with short diagonals, falling, fading)
using integer arithmetic only, so every platform renders identical pixels. It
writes `content/data/gfx/terrain-glintN.png`, the HD frames and
`content-src/gfx/terrain-glint/provenance.json`; `--write-catalog` adds the overlay
blocks, `--check` reproduces frames and blocks, and `--sheet`/`--gif` write a
still and an animated review of the three fertile fields.
`terrain_synth.py --write-catalog` keeps each material's overlay, decor and surf
blocks; `terrain_glints.py --check` reports a missing or outdated overlay.

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

Lava, molten lava pools, and ember fields use four 8×8 layouts with four phases, frame
`cell + 64 * layout + 256 * phase` (frames 0–1023), catalogued with
`animation_frames 4`, `animation_stride 256`, `animation_ticks 16` and
`animation_blend_steps 4` (the recipe's `blend_steps`): the crossfade moves in
four steps per phase, holding each weight for four render ticks. A full loop
takes 64 render ticks. Each layout's crust is shared by its phases; only the glow
ramp moves.

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

#### Stone maps

Laid stone paths set `stone_edges=True` on their recipe, and their geometry
calls `record_stones(ctx, ids, edge, centres)` (the shared `dress` step does so
for every path built on `_paving.py`). `terrain_synth.py --stone-maps` renders
layout 0 of each such path, since every layout shares the stone layout, and
writes `content/data/terrain/stones/<key>.png` together with the material's
`stones` catalog field. Rerun it whenever a path's stone layout changes. The map
covers the whole variant block at HD resolution (four texels per logical pixel).
Each RGBA8 texel holds the texel's stone and the stone across the nearest joint,
each as its number among the stones centred in one cell plus that cell's offset
(`(dx + 1) + 3 (dy + 1)`), and the joint distance in native pixels × 32; a cell
may hold the centres of at most 64 stones, and no stone may reach more than one
cell beyond the cell holding its centre (`terrain_stones.py` checks both). See
[Whole-stone edges](terrain-rendering.md#whole-stone-edges) for the runtime.
Sawn wooden paths such as boardwalk leave the flag off.

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
| `cover` | hedge, thicket, cliffs, glacier | `canopy` |
| `cut` | void_hole, chasm | `crisp` |
| `built` | road, brick_path, sandstone, overgrown_road, boardwalk, slate_ashlar, basalt_causeway, scoria_blocks, rime_ashlar, moonstone_lozenge, frost_pinwheel | `paved`, `crisp` |
| `flow` | lava, ember_field, molten_lava_pool | `molten` |
| `solid` | everything else | `rock`, `soil`, `frozen`, `brush`, `turf`, `sand` |
| `pool` (shoreline) | water, deep_water, dark_water | `still` |

| Profile | Shape | Precedence / coast | roughness / amplitude / feather / speckle / bridge (Q8) | Materials |
| --- | --- | --- | --- | --- |
| `canopy` | patch | 0 | 0 / 0 / 128 / 0 / 512 | hedge, thicket, cliffs, glacier |
| `crisp` | patch | 20 | 96 / 256 / 128 / 0 / 256 | void_hole, chasm, boardwalk |
| `paved` | smooth, wiggle 256 / 128 px, squareness 224 | 10 | – / – / 224 / – / 384 | road, brick_path, sandstone, overgrown_road, slate_ashlar, basalt_causeway, scoria_blocks, rime_ashlar, moonstone_lozenge, frost_pinwheel |
| `track` | smooth, wiggle 768 / 64 px, squareness 128 | 0 | – / – / 320 / – / 512 | none since dirt track became brick path; kept for future tracks |
| `molten` | smooth, wiggle 1536 / 64 px | 0 | – / – / 320 / – / 512 | lava, ember_field, molten_lava_pool |
| `still` | smooth, wiggle 1536 / 128 px | 0 | – / – / 320 / – / 640 | water, deep_water, dark_water |
| `rock` | contextual | 50 | 384 / 896 / 192 / 448 / 192 | boulders, ridge_rock, outcrop, scree, shale, pumice, weathered_sandstone |
| `soil` | contextual | 40 / `shore` | 288 / 896 / 448 / 320 / 512 | dirt, clay, volcanic_ash, mud, mudflat, hardpan, deep_snow, wind_swept_snow, loam, moss |
| `frozen` | contextual | 30 | 320 / 768 / 320 / 128 / 256 | ice, frozen_lake, glacier_ice |
| `brush` | contextual | 20 / `shore` | 320 / 768 / 320 / 384 / 512 | heath, tundra, spring_meadow |
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
| 4 | boulders, hedge, thicket, cliffs, glacier | 88 / 832, a sense of height on the grass side |
| 4 | ridge_rock, outcrop | 72 / 768 |
| 4 | lava, ember_field, molten_lava_pool | no cast; scorch fringe `[214,110,40]` 128/640, `[160,80,40]` 96/512 and a hotter `[255,120,10]` 160/768 |
| 4 | ice (existing), frozen_lake, glacier_ice | 64 / 640 with frost fringe |
| 3 | road (existing), brick_path, boardwalk, sandstone, overgrown_road | 48 / 512 |
| 3 | rime_ashlar, moonstone_lozenge, frost_pinwheel | 24 / 512, a soft cast so their pale stone sits low in the snow |
| 3 | deep_snow, wind_swept_snow | 48 / 512 with cool fringe `[196,210,232]` 64/384, a rim on grass and sand |
| 2 | grass (existing), loam, moss, spring_meadow | 56 / 512 |
| 2 | heath, tundra, arctic_tundra | 48 / 480 |
| 2 | dirt, clay, shale, weathered_sandstone, volcanic_ash, laterite | 40 / 448 |
| 1 | sand (existing), mud, mudflat, scree, pumice, hardpan | no cast |

Casts stay within two to three pixels and near or under a third strength, as
[Seams](terrain-rendering.md#seams) requires. Equal ranks cast nothing, so barren ground beside grass
takes no lip. A lighter inner lip inside a hole cannot be expressed by the seam
model, which only tones neighbours; it would need an engine-side self-lip.

Related: [asset production](README.md).
