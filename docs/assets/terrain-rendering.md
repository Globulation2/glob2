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
`clasts` is `none`, `round` (soil; the shipped sand and snow set `none`) or `angular` (irregular chips at a
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
otherwise ring the water with surf. Against surf a sample leaves out its own
clasts too: the wash front runs at a fixed distance inside the contour, and
inside each rounded grain that line is a crescent of foam. Surf therefore
follows the coast without its chips, and washes over the chips themselves.

In desktop Legacy GL every visible mixed cell takes a coverage mask at the display
density (8 to 128 texels a side), built by background workers nearest the view
centre first. Until a cell's mask lands it draws its mask from the previous
density, if a zoom just changed it, or else its base mask: a low-density mask
of every mixed cell on the map (8 texels a side, 4 or 2 on large maps), laid
out like the map and built by the workers whenever visible cells leave them
idle, nearest the first view first. A cell whose base mask is not built yet,
just after a map loads or an edit, or on a map too large for one, splits
straight between its corners: each border crosses the cell's edges at their
midpoints, a lone corner is cut off along the diagonal and diagonal corners keep
their quadrants, without seams or surf. The previous atlas is
released once the new density covers the view; its textures are reused by the
next zoom.

Surf reads the same per-sample distance as seams, which is exact only within
eight logical pixels of the edge, so each band and `reach_q8` plus
`foam_width_q8` must end within 2048. Water/sand edges use the `beach` coast
profile, a low-roughness contour, so the waterline is a gentle curve the wash can
follow instead of a row of teeth. A material with surf counts as
animated: its pages re-blend whenever the wash advances, also without texture
animation. The optional `ticks` (default 1, at most `period`) advances the wash
of composed pages only once per that many animation ticks, so a material with
still artwork can re-blend its pages about ten times a second (`ticks` 6)
instead of every frame. The shipped waters all animate and re-blend every frame
for their texture crossfade, so they keep `ticks` 1. Surf is presentation only
and never touches the simulation. `TerrainVisual::applySurf` is the integer
reference that the `terrain-compose` shader mirrors exactly. The direct pass
moves the wash continuously instead: its phase is the reference's at every
whole surf step and interpolates linearly between them (see
[animation clock and smoothness](#animation-clock-and-smoothness)), so it
matches the reference to well under one 1024th of a wash at whole steps.

## Caches and compatibility

Saved maps hold terrain IDs per vertex, not sprite frames, so changing visual
variants in the catalog cannot change saved state, checksums or simulation RNG use.
Files older than format 146 also stored sprite frames; the loader skips them, and
only `LegacyTerrainFrames.h` still decodes classic frames, for old script memories.

`TerrainVisual::Compositor` owns prepared material sources. Source lifetime and
content revisions, animation phase and native/HD selection invalidate prepared
pixels. View caches hold square composed pages and compare recipes including
the surrounding lattice, so edits update neighboring tiles and wrapped chunks.
A page is 512 pixels wide at every density: 16 cells at native density, 8 at
twice and 4 at four times (`SoftwareTerrainCache::pageTilesAt`). A view at
three or four times density (zoom 1.5 or 2 on a 2x display) then needs little
more than its own area in pages and fits the budget below, where 16-cell pages
(2048 pixels at four times) cost up to twice that for their alignment and
dropped the view to twice density, a softer image than the other renderers' HD.
Each page and tile tracks revisions only for materials used by its discovered
recipes; an animation outside that dependency set does not rebuild the page.
Sampling density is part of the page key. Old densities retain their composed
CPU pixels inside the cache budget. Renderer allocations also remain reusable
until memory pressure retires them. Returning to a density revalidates terrain,
discovery and material revisions before reusing its pixels.
The historical `SoftwareTerrainCache` name is retained for benchmark controls,
but the cache also draws GPU pages. Software page storage stays bounded by
32 MiB. GPU density selection retains its 128 MiB allowance, and resident
textures remain bounded by 128 MiB of conservative texture reservations: a
page's texture and its mips (three times its pixels with the CPU copy), or
twice its pixels on the portable SDL renderer, whose page textures have no
mips. `TERRAIN_PAGE_ADMISSION` on stderr records each change of a view's page
density, page size and the reason it is coarser than the compositor's
(`page budget`, `texture size`).
On desktop, the total GPU-page cache has a 256 MiB ceiling including CPU pixels,
bookkeeping and resident texture reservations. The additional 128 MiB lets
inactive zoom densities retain their CPU pixels without sacrificing sampling
quality. Android and browser builds retain the 128 MiB total ceiling.
These are maximum memory allowances, not up-front allocations.

Interactive views on accelerated page renderers without device composition (the
portable SDL renderer, and browser legacy OpenGL composing HD pages) never
compose a page inside the frame. A cold page is described from the scene on the
graphics thread and composed by the shared presentation workers
(`render/BackgroundWork.h`) from an immutable material snapshot; a page at a
reduced density whose native page is retained is reduced on the workers from a
copy of that page instead, except its mixed cells, which off the render devices
compose at the reduced density (`Compositor::compose` with a reduction) so their
shore lines match the direct pass. There are half as many workers as logical CPUs,
between two and four: at normal game speed the simulation needs about one core,
and in the pan-and-zoom benchmark more than four workers neither filled missing
pages faster nor kept frames shorter, because the graphics thread's per-frame
launch and install budgets set the pace. `GLOB2_BACKGROUND_RENDER_WORKERS=N` (1 to 64)
overrides the count for measurements. Until it
arrives the view draws the same place at its nearest other density (the larger
page holding it, clipped to it, or the smaller pages within it), or the
overview beneath where none, or only part, exists; the overview then draws, and
composes, only those missing pages. The portable renderer also keeps a resident
base: the whole map in native 16-cell pages reduced to 16 pixels a cell (8 or 4
on maps too large for half the platform's `TerrainPage` texture allowance:
64 MiB on desktop, 32 MiB with the mobile budget), composed two pages a frame
from the view outward on the workers' lower lane from the first interactive
frame, uploaded on arrival with their CPU pixels released, and never evicted. A
missing page whose other densities do not cover it draws its base page, with any
nearer partial density over it, so a fast zoom or jump shows textured terrain at
no coarser than 16 pixels a cell rather than the flat-coloured overview. A base
page whose cells' terrain or discovery changed draws nothing and composes again;
its animated cells animate like any page's (see
[Animated terrain on the portable renderer](#animated-terrain-on-the-portable-renderer)). A page that arrives where the view drew either fallback
fades in over it in 150 ms rather than popping. Pages around the view also
compose ahead of the camera: a ring one page wide, widened in the direction of a
pan by about half a second of its recent motion and all round while the view is
growing in a zoom out. Those launches wait until every visible page has
arrived, use only the frame's leftover launch budget and half the in-flight room, run on the workers' lower priority lane
(`BackgroundWork::Priority::Ahead`, taken only while no visible work waits), and
admit a finished page only into free cache room or room held by pages the view
has not used for two seconds, so they never displace a stand-in. A page composing
ahead that the view reaches is queued again as visible work; each job runs once,
whichever lane takes it. Visible pages install before pages composed ahead. On
browser Legacy OpenGL composing HD pages, uniform animated cells (open water) draw over
their page from one shared tile per material variant at the page's density, so
an animation step recomposes and uploads those few tiles, not the pages. Any
other animation or surf step of a valid page (shorelines) queues its changed
cells the same way while the view keeps drawing the current pixels; finished
cells upload as dirty rectangles. The portable renderer animates from keyframes
instead (below). Each frame describes at most
4096 cells for launches (a native page copied for a reduction counts as 64)
within 2 ms of graphics-thread time spent describing (one launch always goes),
keeps at most four visible pages per worker plus four in flight and 64 MiB of
page buffers, and installs at most sixteen finished pages within 8 MiB. Missing
pages that nothing stands in for (where the overview shows) go first, then
those nearest the view centre, and work for pages the view left is cancelled. Missing pages take build
slots first: refreshes use at most half of them, and none while a missing page
waited for a slot the frame before, so animated water cannot starve the edges
of the view. Pages record the discovery
revision they were checked against, so unchanged views skip per-cell fog checks.
The compositor caches the material variants each 16×16 page uses until the
terrain revision changes, so preparing a view unions short lists rather than
visiting every cell. On the portable renderer a drawn page that is not
native density and has nothing animated outside the shared tiles keeps only its
texture: its CPU pixels go, and a change, an evicted texture or a renderer reset
composes it again. Pages neither drawn nor standing in for about ten seconds
leave the cache, as do animated tile sets, and the last material snapshot goes
two seconds after the last launch. The editor, tiled captures, offscreen draws
(map exports) and `DRAW_COMPLETE_TERRAIN` draws still compose every page before
drawing.
Generated/edited pages keep alpha-weighted mip filtering but upload uncompressed
textures. Driver-side DXT encoding is reserved for artwork with prepared mip
chains; repeating that encoding on animated terrain updates stalls rendering.
The direct art atlases and cached animated tile atlases reserve a one-texel
gutter for linear filtering. An animated material with one positional layout
takes each gutter texel from the adjacent tile in that layout, including the
diagonal at each corner and the wrap at the layout boundary. Other materials
repeat their outer texels. The gutter affects artwork sampling only, independently
of shoreline coverage and surf.

Crossfades update material revisions on each visual tick. CPU-composed animation
pages recompose at that cadence; `animation_blend_steps` lowers it
to that many recompositions per phase (see
[terrain materials](terrain-materials.md)). Repeating the same animation time
reuses their pixels. Interactive HD terrain on Legacy OpenGL draws animated
cells, including mixed shorelines, through the atlas shader. A separate source
atlas holds visible current/next HD phases within 16 MiB and blends every frame.
While a phase draws, the workers filter the endpoint after next
(`Animation::Entry::ahead`), so the step that needs it writes it at once: a
step never draws a stale endpoint while its new one is filtered.
Static page draws omit those complete cells, so alpha and fog apply once.
Shore calming fades wave contrast over four tiles from the shoreline, keeping an
eighth of it at the shoreline itself.
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
textures in drawing order as needed. Before retiring a texture the frame draws,
an upload retires idle textures and then drops idle pages that hold only pixels
(such as HD pages parked before a zoom out); when every remaining page is drawn
this frame, the page waits for a later frame instead of failing it. This preserves the selected sampling and
avoids repeated boundary composition near page-alignment budget thresholds.
Only a view whose CPU pages and one upload cannot fit uses composition streaming.
First-time composition still evaluates native terrain before reduction; this
policy removes repeated work on warm frames, not the cost of a cold frame.

Overview palette samples have a separate 48 MiB page cache (CPU pixels and the
texture each page uploads). A view larger than that keeps all its pages, and the
cache drops back to the budget once the view is smaller again. Interactive views
compose overview cells on the presentation workers within a per-frame budget
(1.5 ms of graphics-thread time describing cells). Beneath cells still composing
the view draws the other density where it has them and otherwise the whole-map
base: one linearly filtered texel per cell, the mean of its corners' overview
colours, composed by a worker from the snapshot's immutable vertex storage as the
map loads and again after a terrain edit (4 bytes a cell, twice for its texture;
2 MiB for a 512 by 512 map). The overview therefore never shows a transparent
patch once the base is ready, a frame or two after the map loads. A page whose
first cells arrive after it was drawn empty fades in over the base in 150 ms.
A page covers 64 by 64 cells (the whole map when it is smaller), so a whole 256
by 256 map is 16 page textures. From the start of a game the cache also prewarms pages at the density the
farthest zoom draws, spiralling out from the view's centre within three
quarters of its allowance (less what the view holds) on the workers' lower priority lane, and
starts the spiral again around a new centre after a jump or a pan of two pages;
a prewarming batch for a page the view then waits on is queued again as visible
work. On a presentation device the complete prewarmed pages are then admitted to
the device adapter ahead of the zoom, nearest the view first, with the upload
allowance the frame leaves, so a zoom out draws all of them on its first frame (that
frame copies them to the GPU, 4 MiB for a 256 by 256 map at four samples). Describing cells stops at the frame budget
even within a page, checked every 64 described cells. Captures, exports and other one-shot draws compose every cell first. Its pages keep canonical world coordinates across
viewport movement, atlas tiles and zooms that keep the same samples per cell. Vertex windows include the contextual
contour halo; terrain seeds, immutable terrain/resource registries and map asset
bundles also invalidate the samples. Texture animation and blend opacity do not
change the overview palette.

A cell takes 8×8 samples once it covers at least six device pixels on screen and
4×4 below that (switching back below five), so the image never holds much more
detail than the display can show. A view whose 8×8 image would exceed the
device's maximum texture size uses 4×4. A density change replaces every page.

A resource leaves the terrain colour visible and adds a square in its minimap
colour, centred in the cell, that grows with the cell's stock out of the
resource's total yield capacity. Its side is 1 sample at the lowest stock and
rounds down, so only full stock reaches half the cell: 1, 1, 1, 1, 2 samples at
4×4 and 1, 2, 2, 3, 4 at 8×8 for stocks 1 to 5 of 5. An odd leftover sample
falls below and to the right of the square, so a 1-sample square sits just
above and left of the centre and the gap between neighbouring squares is half
a cell at full stock. A cell is
recomposed when its resource type, square size or discovery changes.

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

## Animation clock and smoothness

Water, surf and lava move by exactly the time elapsed on every presented frame,
at any frame rate, and in step across the view and the renderers.
`AnimationClock` (`src/render/AnimationClock.h`) counts real time in steps of
1/60 s, the rate the materials' `animation_ticks` were tuned at; each frame it
returns the whole steps and the fraction of the next step already elapsed. A
view's presentation time (`MapRenderState::animationNow()`, an `AnimationTime`)
is those steps plus that fraction, the same for every consumer of the frame. A
long frame counts as at most 0.1 s, a pause freezes both parts, and
`fixedSteps` (whole steps, no fraction), `simulatedFrameNs` (a fixed frame
interval, fractions included) and `presetNext` keep tests and captures
deterministic.

What reads it:

- **Direct pass** (SDL GPU, Programmable GL, Legacy OpenGL). `DirectTerrain`
  hands the shaders the time wrapped at `TerrainVisual::animationPeriod`, the
  least common multiple of every flipbook cycle (frames × ticks) and surf period
  of the catalogue (81600 ticks, 22.7 minutes, for the shipped one). Every
  animation repeats within it, so the wrap is seamless, and a float resolves
  1/128 of a tick there however long a session runs. `material()` in
  `DirectTerrainShader.h` crossfades by the elapsed share of the flipbook frame,
  and `surf()` takes the wash phase from the continuous time; authored blend
  steps and surf `ticks` quantise composed pages only.
- **Portable SDL** draws each animated cell's next keyframe at the elapsed share
  of the keyframe interval (`AnimationTime::cycle`,
  `SoftwareTerrainCache::keyframeAlpha`), from the same presentation time.
- **Whole-step consumers** read `steps`: flipbook frame selection and the HD
  streaming window, composed pages (the software renderer and browser Legacy
  OpenGL, which stay correct and in phase but move only on frames that cross a
  step), fertile-glint overlays, area markers and clouds.

These guards must stay green; they fail if any link of the chain rounds time
back to whole steps:

- `AnimationClock` unit cases (headless): at 30, 60, 120 and 144 Hz, with and
  without jitter, every frame's presentation time advances by exactly the
  elapsed time; crossfade weights move by the elapsed share of their interval;
  the wrapped time stays continuous and precise after a month of steps.
- `TerrainAnimation` headless cases: the shipped catalogue's animations all
  repeat within its wrap period; the direct pass's crossfade weight and wash
  phase, computed as `DirectTerrainShader.h` computes them (whose expressions the
  case also pins) from the uniform `DirectTerrain` passes, move by exactly the
  elapsed time every frame and across the wrap; the portable keyframe alpha
  moves by the elapsed share every frame.
- `TerrainAnimation` display cases, two per renderer: a sand island in water
  drawn through the whole map draw path for 24 frames at each rate, starting
  just before the wrap. The first case waits until terrain has settled at each
  frame's time; the second draws the frames back to back at the display's pace
  while background work lands when it lands. For each frame, the crossfade
  weight the open water shows between the two flipbook frames around its time
  (a least-squares fit over the channels those frames differ in by 12 levels or
  more, so 8-bit rounding averages out) must be within 0.03 of the share of the
  flipbook frame elapsed. The water and the wash change every frame; the wash
  moves at a steady pace with no shoreline flashes; open-water cells in
  different pages that matched at the start stay identical. The software
  renderer instead shows the weight of the whole step and changes exactly on
  the frames that cross a step. The mean change of one 8-bit frame is no
  measure of smoothness at high frame rates: a water frame differs from the next
  by a few levels, so most pixels move by less than one level a frame, and
  rounding, not the animation, sets which frames change most. A last case
  draws the same scene on every renderer at a whole step and half a step later
  and checks they agree, and that each interpolating renderer's half-step image
  is nearer the reference's at that time than at the step before.
  `GLOB2_ANIMATION_TEST_RENDERERS` limits the renderers on a machine where some
  must not run.

## Animated terrain on the portable renderer

The portable SDL renderer has only 2D textures and blend modes, so it cannot
evaluate the animation per pixel as the direct pass's shader does. Its
interactive views keep animation out of the pages altogether: a page holds
still pixels, and animation changes no page's recorded revisions. Every
discovered cell that an animated material covers, or whose surf band reaches
from a halo neighbour, draws over its page each frame as a crossfade of two
**keyframes**: its composition at the animation times `k * K` and
`(k + 1) * K`, where `k` is the current time divided by `K`, the shortest
`animation_ticks` of the catalogue's animated materials (6 for the shipped
waters; 6 for a material whose surf alone moves). The keyframe `k` draws at the
page's opacity and `k + 1` over it at the fraction of `K` elapsed, the weight the
direct pass blends its two endpoints with. Composition is linear in each
animated texel, so this crossfade equals the composition of the crossfaded
textures: for water, whose blend steps equal its ticks, it is the direct pass's
image to within 8-bit alpha rounding. The weight comes from the
presentation time, fraction included, so the crossfade moves every frame (see
[animation clock and smoothness](#animation-clock-and-smoothness)). Surf is
interpolated linearly between keyframes 0.1 s apart, well under a pixel of
wash. Lava (16 ticks, 4 blend steps) is sampled every 6 ticks and so blends
between its own composed steps: smooth, and within an eighth of its frame
difference of the direct pass's continuous crossfade.

Keyframes compose on the presentation workers from a material snapshot of the
compositor prepared at the keyframe's time: the view prepares the compositor
for `k + 2`, the keyframe needed next, so each one composes a whole interval
before it shows, and keeps the snapshots of `k` and `k + 1`. A missing snapshot
(after a jump, or with the clock stopped) is prepared first; one taken from older
artwork (HD frames that have since arrived, or changed still materials) or
without a variant a cell needs is prepared again at most every ten frames, and its
cells' keyframes compose again. A uniform uncalmed cell shares one keyframe per
variant with every cell of its material at its page's density; a mixed,
calmed or halo cell has its own, composed from the page's kept coverage mask
and, where an earlier keyframe of the cell holds the same still artwork, only
over the samples animation changes. Each set of keyframes is an atlas with a
band per keyframe of a three-slot ring, every slot bordered by a copy of its
edge for linear sampling; the keyframes a frame installs upload as one rectangle
per band. The cells the view shows and a margin of two cells want the keyframes
`k` to `k + 2`, the current one first, at most 2048 cells a frame and 8192 in
flight. Stand-ins (other densities and the resident base) animate the same way
at their own density. A cell with neither keyframe yet shows its page's still
pixels; a page arriving over a fallback holds its fade, for at most 500 ms,
until its visible animated cells have keyframes. Keyframe sets of pages the view
left for two seconds, and shared sets unused for ten, are released.

Related: [asset production](README.md).
