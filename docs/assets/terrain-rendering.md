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

### Boundary rules

Version-4 catalogs choose the profile of every contact from what the two
materials are, without per-pair entries. `border_classes` lists the classes
strongest first; each material names its `class` and its edge `profile`:

1. The stronger class shapes the contact with its own edge profile. The shipped
   order is `cover` (raised foliage that hides its edge), `cut` (holes and
   chasms), `built` (roads, tracks, boardwalks), `flow` (lava, which overruns
   its surroundings), `solid` (natural ground) and `pool` (standing water).
2. A class marked `"shoreline": true` takes the shape of its basin: against it
   the stronger side draws its edge profile's `coast` when it names one. Grass
   therefore meets water with `shore`, sand with `beach` and rock with `rock`,
   while lava and roads keep their own edges.
3. Within one class the edge profile with the higher `precedence` (0–255) wins,
   with the profile key as a stable tie break: rock over soil over ice, brush and
   turf; the three waters all use `still`.
4. A `pair_treatments` entry still overrides one pair. The shipped catalog needs
   none.
5. A class marked `"cuts_stones": true` cuts laid stone paths along its smooth
   edge instead of meeting them on whole stones; see
   [Whole-stone edges](#whole-stone-edges).

`python3 tools/terrain_tileset.py --border-table` lists the profile of every
material pair; `--compare OLD.json` lists only the pairs that changed. Version
1–3 catalogs keep the earlier rule: the rougher profile wins, and contextual
geometry requires both materials' profiles and the chosen one to opt in.

### Contextual natural borders

Version-3 profiles may set `"shape": "contextual"`; omission or `"patch"` retains
existing patch geometry. The shipped rock, soil, frozen, brush, turf, sand, shore
and beach profiles use contextual geometry. In version 4 a contact uses it when
its chosen profile does; the class order keeps natural profiles from rounding
roads, holes and lava.

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

[Boundary rules](#boundary-rules) choose the profile of a contact. Optional
`pair_treatments` entries contain `a`, `b` and `profile`; these choose another
contour family for that pair without changing material connectivity. Pairs are
unordered, unique and must reference existing materials and profiles.

### Smooth borders

Version-4 catalogs draw every contact with smooth geometry, whatever the
profile's `shape`. One algorithm and one distance measure therefore cover every
tile edge, whichever terrains meet, and a new terrain needs no boundary code of
its own: its profile only sets parameters. The patch and contextual geometry
described above remain for version 1–3 catalogs.

A profile takes `wiggle_q8` (0–2048, the Q8 pixels a straight contour meanders),
`wiggle_period` (32, 64, 128 or 256 logical pixels between the broadest bends),
`squareness_q8` (0 round to 256 square corners), `detail_q8` (0–1024, Q8 pixels
of fine roughness on a 16-pixel lattice) and `jagged_q8` (0–2048, Q8 pixels of
light grit on 16- and 8-pixel lattices, the finer ridged). These are noises of
each material's own field, so they roughen both sides alike.

Most of a natural edge's shape comes from its **teeth**, which belong to the
contact rather than to either material. One side of every contact shapes it
(`Catalog::shapes`): the dominant material by border class and precedence, as
in the real world, where the resistant side of an eroding contact keeps the
headlands, the land shapes a pooled liquid's shore, and an advancing flow pushes
its own front. `teeth_q8` (0–2560) of the contact's profile sets the
displacement in Q8 pixels. The teeth are value noise of world position on 32-,
16- and 8-pixel lattices along the edge, most of the energy at about one
feature per tile edge and falling steeply at finer scales, folded at zero into
crests. `teeth_form` chooses the polarity: `points` (the default) push sharp
crests of the shaping material into its neighbour, with rounded bays between;
`lobes` turn them round, so rounded tongues advance with sharp cusps pointing
back, as lava fronts do. `facets` interpolates the teeth linearly so their
flanks run straight, as broken rock does; the finest octave is faceted always.
The lattice is fine along the edge and 128 pixels across it, blended by the
bare splines' slope direction, so a contour displaced by teeth cannot fold back
into islands or overhangs.

**Clasts** are grains a material sheds across its own edges, whatever it meets:
`clasts` is `none`, `round` (sand, soil) or `angular` (irregular chips at a
hashed rotation, for rock and ice), and `speckle_q8` of the material's own
profile sets their reach. Half the cells of an 8-pixel world grid hold one,
2.5 to 5.5 pixels across, and each pushes its material's border out by a hashed
share of twice the reach, so fewer grains reach the far side. Turf and brush
shed none.

Teeth and clasts are added to the pair's distance antisymmetrically, so
weights still sum to one and seams and surf follow them, and they fade in only
where both fields have risen, so they never reach a vertex or a uniform
neighbouring cell. `feather_q8` and `bridge_q8` keep their meanings.

A rough profile that names none of these takes them from its contour strength,
`amplitude_q8 × roughness_q8 / 256`: a quarter of it as `wiggle_q8`, an eighth
as `detail_q8`, that strength times `(roughness_q8 + 256) / 512` as
`jagged_q8`, and twice it as `teeth_q8`. From `roughness_q8` 320 its teeth are
faceted and its clasts angular; below, a profile with `speckle_q8` sheds rounded
grains. Rock therefore breaks up most, soil, sand and turf roughen, and a beach
is only slightly irregular. A profile without roughness (foliage that hides its
own edge) gets square corners. Smooth profiles (roads, tracks, liquids) keep
their clean lines; lava sets lobed teeth. `roughness_q8` and `contours_q12` no
longer shape version-4 contacts directly.

### Physical properties

A version-4 profile describes what its edge family is made of, and the contact
parameters above follow from that, so a new terrain names a few physical
numbers rather than tuning contours. Each is Q8 from 0 to 256 except `grain_q8`:

| Property | Meaning | Drives |
| --- | --- | --- |
| `resistance_q8` | Hardness against erosion and wear | Who shapes a contact within a class (`precedence`), faceted teeth and angular clasts from 128, grit (`jagged_q8` = 256 + 6 × resistance) |
| `cohesion_q8` | How well the material holds together (roots, binding) | With resistance, the teeth: `teeth_q8` = 384 + 9 × (resistance + cohesion) / 2, so loose sand makes smooth arcs and rock a broken edge |
| `grain_q8` | Q8 pixels its loose grains reach (0 for continuous material) | `speckle_q8` and `clasts` |
| `spread_q8` | How much it advances over its neighbours (flow, growth) | Lobed teeth from 128 |

Meander and fine detail follow the teeth (`wiggle_q8` an eighth of them,
`detail_q8` a sixteenth). Any parameter a profile names explicitly overrides
its derived value, and profiles without physical properties (built surfaces,
canopy) keep their explicit parameters. Border classes still give the order
across forms (cover, cut, built, flow, solid, pool), and coast profiles still
let the land draw its shore with its own physical properties.

The shipped natural families:

| Profile | Resistance | Cohesion | Grain | Spread | Result |
| --- | --- | --- | --- | --- | --- |
| `rock` | 230 | 230 | 448 | | Large faceted teeth, angular chips |
| `frozen` | 150 | 150 | 128 | | Faceted teeth, fine angular chips |
| `soil` | 100 | 180 | 320 | | Rounded teeth, rounded grains |
| `brush` | 80 | 200 | | | Rounded teeth, no grains |
| `turf` | 64 | 220 | | | Rooted edge that shapes loose sand |
| `sand` | 24 | 40 | 512 | | Gentle arcs, scattered grains |
| `shore` | 40 | 140 | | | Vegetated or soil coast |
| `beach` | 10 | 20 | | | Smooth beach arcs |
| `molten` | 60 | 160 | | 230 | Lobed lava fronts |

The noises of one field together stay within the bound that keeps every field
positive where its vertices reach, scaled down alike when a profile asks for
more.

Seams and surf need the distance to a contour that may lie beyond the tile
edge. A cell therefore also gives a field to each material of its 4×4 vertex
window that is no corner but draws a band against one (`Recipe::halo`, at most
three, in free palette slots): a seam shadow, a fringe or surf
(`Catalog::bandsBetween`). Halo fields never take coverage, so weights are
unchanged; they make the distance and band neighbour equal on both sides of
every tile edge, and a cell whose corners all agree draws the bands of a
contour just beyond it instead of cutting them off. Distances measure only to
materials that draw a band with the sample's material, so a contour without a
band neither hides one behind it nor depends on which tile measures, and skip a
field that has not risen at the sample. They use the smooth slope of the
fields: fine detail and grit move the contour through the score, but a
first-order distance extrapolating their steep slopes would put every contour
within a few pixels.

`SmoothBorders` gives each corner material of a cell a field interpolated
through the 4×4 vertex neighbourhood with Catmull-Rom splines; the strongest
field owns a sample. The splines pass through their vertices, so every vertex
keeps its own material, and their slopes agree across cell edges, so a contour
continues across the map instead of restarting in every cell. Ambiguous
diagonals and junctions need no fallback: the fields decide them. At a diagonal
saddle the dominant material (stronger class, then precedence, then a seeded
choice) joins its two lobes through a neck sized by `bridge_q8`, so roads and
rivers continue diagonally.

Squareness remaps each cell coordinate toward a step at the cell's midline (a
blend of the coordinate and a triple smoothstep) before the splines are
evaluated. The remap keeps both cell ends and one slope at each, so fields stay
continuous across cells while contours hug the midlines and corners tighten from
arcs toward right angles; roads use 224, tracks 128 and liquids and lava 0.
Corner shares take the squarest smooth contact around a vertex and interpolate
like wiggle. A square vertex whose 3×3 neighbourhood is two materials split by a
45-degree line (a staircase edge, or one step off it) takes a squareness of -0.2
instead, which runs the contour along the staircase straight to within a pixel.
Right-angled corners keep their profile squareness, and round profiles are
unchanged. Shallower staircases (2:1 and steeper) still step.

Meanders multiply each field by world-anchored value noise of the material's own
`wiggle_period`, in two octaves. Noise never reaches a zero field, so a material
cannot appear where its vertices do not reach. The pair's meander is the
difference of its two fields' noise, at most `wiggle_q8` along a straight
contour and bounded so a vertex always wins its own sample. Weights ramp over the
two materials' mean feather from a first-order distance to the competing field,
and seams and surf read that distance to the nearest field of any neighbourhood
material, including vertices beyond the cell edge.

Seams and surf follow the teeth, which shift the pair's distance, and measure
across the contour with the smooth slope.

In version 1–3 catalogs, a vertex is smooth when every contact with its eight
neighbours resolves to a smooth profile. A cell interpolates its four corner
shares bilinearly (with a smoothstep) and blends smooth coverage with the patch
or contextual coverage by that share.
Version-4 cells with more than one material read the one-vertex halo; earlier
catalogs keep halo-free recipes outside two-material contextual cells.

Textures are kept separate from boundary masks. Runtime preparation blends each
material's variant borders toward one periodic master while keeping interior
texture detail. The four-pixel border blend operates on premultiplied RGBA, so both
color and opacity agree across variants without introducing dark transparent fringes.
The compositor does not bake a texture-by-mask-by-material-pair product.

### Whole-stone edges

Laid stone paths end on whole stones: where trail, brick path, mossy setts,
sandstone tiles or the ashlar, causeway, scoria, lozenge and pinwheel paths meet
other ground, each stone is kept or dropped as a unit, so the edge runs along
the joints instead of cutting a stone. Boardwalk keeps the cut border, as sawn
planks would. A material opts in with `"stones"`, the data path of its stone
map (see [Stone maps](terrain-production.md#stone-maps)).

A stone follows the smooth border at its centre: it is dropped only when land
covers more of its centre than the stone material does. The decision is made
once per stone, not per pixel, from the cell holding its centre, so every cell
the stone reaches agrees. `Compositor::describe` records, for a cell and its
eight neighbours, one bit per stone centred in each (`Recipe::StoneLayer::kept`),
because a stone reaches at most one cell beyond the one holding its centre.
Cached pages that compare recipes rebuild when any decision changes.

Each stone path whose stones can reach a cell is one of its stone layers, up to
three (`Recipe::stoneLayers`): paths among the cell's own corners first, then
the dominant one. Layers apply from the last to the first, so where two paths
meet the dominant path's kept stones lie over the other's and the subordinate
is cut along their outline. Other stone paths never drop a stone, so two paths
leave no gap between them.

Some border classes cut stones instead (`"cuts_stones": true`; the shipped
`cut`, `flow` and `pool` classes): water, lava, holes and chasms never drop a
stone and are never revealed. Against them the path keeps its stones and the
smooth edge cuts them, as a quay or a drop would, and samples a cutting material
covers keep the smooth border (`Recipe::stoneCutCorners`).

A dropped stone reveals the nearest land: the most common land among the cell's
corners, else its 4×4 window, else its 6×6 one. Paths are discovered from that
6×6 window (vertices −2…3), because a stone centred in a neighbouring cell
reaches this one from that cell's window; `Compositor::prepareVisible` prepares
every material of the same window near a stone path, so no layer or ground
draws from an unprepared texture. A cell whose palette cannot hold its corners
and a layer's stone material and ground leaves that layer out; the continuity
test counts any stone this leaves inconsistent.

Sampling a pixel then costs one stone-map lookup and a bit test. A kept stone's
face fades into the ground it borders over one logical pixel, half on either
side of the joint. The seam distance measures from the stone edge, so the path's seam
cast draws a contact shadow along the stones. Kept stones may overhang into a
cell whose corners all show other ground, so such cells are not uniform
(`Recipe::uniform`) and draw a mask; their palette lists the stone material and
the revealed ground after the corners (`Recipe::palette`). Every pipeline,
software and GPU pages at any oversampling and direct terrain, reads the same
masks. Overview recipes leave stones out.

Stone maps are decoded on first use (four bytes per texel, 4 MiB for an 8×8
block at HD resolution) and kept; a map without stone paths decodes none.

### Seams

Textures are never cross-faded across a boundary; a wide fade averages unrelated
textures into a smear. Instead the compositor tones a narrow contact band, as the
original tiles did with a dark lip on sand under grass and a wet line along the
shore. The resolver reports, per sample, the nearest other material and the
distance to it: the Euclidean distance to the contour curve for contextual borders
(also in their one-pixel cell-edge band), otherwise an estimate from the patch
score gap, which grows by 12288 per Q8 pixel along a straight edge. Each material's optional `seam` object sets `height` (0–255, a
stacking rank), `cast_q8` (0–256) and `cast_width_q8` (0–2048 Q8 pixels): a
material darkens lower-ranked neighbors by up to `cast_q8`/256 at the contact,
fading to nothing at that width. Equal ranks cast nothing, so sand beside sand
of another variant is untouched. `fringe` (RGB), `fringe_q8` and
`fringe_width_q8` tint any neighbor toward that color, which ice uses for a
faint frost rim on grass. The shipped ranks place water highest so sand and
grass take a dark contact line at the waterline, then ice, trail and grass, with sand
lowest. Keep casts short
(two to three pixels) and under about a third strength; the goal is a sense of
thickness, not an outline.

### Surf

A liquid material's optional `surf` object animates its shoreline wherever it
meets a material without surf; liquids beside each other (water and deep water)
take none. On the liquid side, `shallow` (RGB) tints the water by up to
`shallow_q8`/256 at the edge, fading over `shallow_width_q8`. On the land side,
`wet_q8` darkens a wet band `wet_width_q8` wide. Every `period` animation ticks a
wash climbs up to `reach_q8` onto the land, covering it with a thin sheet of the
shallow color, and drains back; `foam` (RGB) at `foam_q8` marks its leading edge
over `foam_width_q8` on either side. Washes rush up over the first 30% of the
period and drain over the rest, staggered along the coast by a smooth world-space
field whose 256- and 512-pixel wavelengths divide every map size, so the pattern
wraps cleanly; another field breaks the foam line into patches.

The land side sets how far the wash breaks on it: a material's optional
`shore_wash_q8` (0–256, default 256) scales the wash's reach, and so how far it
swings with each wave, wherever a liquid's surf meets it. The bands (shallows,
wet band, sheet and foam) keep their full strength, so the shoreline stays as
visible as a beach's. Laid paths (the `built` border class: trail, brick and
stone paths and boardwalk) take 77, about 30%, so water laps against a quay or
a road instead of washing over it. The page shader receives each palette slot's value
in `surfLand`; direct terrain stores its damping (256 minus the wash) in the
otherwise unused fourth field of the foam record, so hand-built records keep the
full wash.

Seams and surf measure the distance to the shaped contour: the smooth field's
distance plus the contact's teeth and the sample's own clasts, divided by the
slope of that whole shaped distance. Teeth and clasts change within a few
pixels along an edge, so without their slope a band would stand wide in front
of each crest and vanish along its flanks. A sample counts only its own
material's clasts: a neighbour's grain that stops short of the surface would
otherwise ring the water with surf.

In Legacy GL views at more than native density, every mixed cell takes a
full-detail coverage mask (32 to 128 texels a side, built by background
workers nearest the view centre first); until a cell's mask lands it uses the
native 16-texel mask, which squares off teeth and clasts.

Surf reads the same per-sample distance as seams, which is exact only within
eight logical pixels of the edge, so each band and `reach_q8` plus
`foam_width_q8` must end within 2048. Water/sand edges use the `beach` coast
profile, a low-roughness contour, so the waterline is a gentle curve the wash can
follow instead of a row of teeth. A material with surf counts as
animated: its pages re-blend whenever the wash advances, also without texture
animation. The optional `ticks` (default 1, at most `period`) advances the wash
only once per that many animation ticks; dark water uses 6, so its otherwise
still pages re-blend about ten times a second instead of every frame. Water's
pages already re-blend every frame for its texture crossfade, so it keeps
`ticks` 1 for the smoothest wash. Surf is presentation only and never touches the
simulation. `TerrainVisual::applySurf` is the integer reference that the
`terrain-compose` shader mirrors exactly.

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
Crossfades update material revisions on each visual tick. CPU-composed animation
pages recompose at that cadence; `animation_blend_steps` lowers it
to that many recompositions per phase (see
[terrain materials](terrain-materials.md)). Repeating the same animation time
reuses their pixels. Interactive HD terrain on Legacy OpenGL draws animated
cells, including mixed shorelines, through the atlas shader. A separate source
atlas holds visible current/next HD phases within 16 MiB and blends every frame.
Static page draws omit those complete cells, so alpha and fog apply once.
Shore calming fades wave contrast over two tiles from the shoreline.
Device composition keeps up to 1024 material source
textures and never evicts one the current frame already drew, so a view with
several four-layout materials (256 variants each) does not re-upload its own
sources within one frame. Pages also keep the coverage of mixed cells that touch an animated material (`Compositor::CellMask`, 10 bytes per
composed pixel), so a phase change re-blends their textures instead of
re-sampling the boundary; coverage depends only on the cell, never on the phase.
These masks have their own budget (16 MiB software, 32 MiB GPU, or 128 MiB for
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
