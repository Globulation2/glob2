# Rice terraces

## Play contract

Every colony owns a grass summit surrounded by concentric crop and water contours,
lobed like a real hill's rather than drawn with compasses.
A small starter inn holds ten wheat for the first feeding cycle; ongoing meals
come from the terraces.
Five-corner-wide radial sand stairs carry workers and attackers across the terraces.
One starting tower per stair covers its upper approach. Its magazine and finite
stone reserve start full, leaving opening workers available to establish food. These are drawn contours;
the engine has no elevation bonus. Swimming bypasses irrigation ditches, while crops
still require clearing. The whole climb is longer than a summit tower's range.

The summit has radius 16 and a two-corner sand containment ring. Complete crop/water
bands end in another sand cap, keeping growth away from both summit and commons.
Beyond the summit's cap the bands lobe in and out by up to four tiles, the same shift
for every band at a heading so their widths hold; the summit and its cap stay round.
The open valleys offer fruit, quarry outcrops and expansion room. Optional vacant
hills provide another farm and summit to contest. A valley river follows boundaries
between hills and has regular sand fords. Players choose which stairs to defend and
which valley to develop; stairs are permanent terrain, without a new gate mechanic.

## Controls and fitting

- **Unoccupied hills:** 0–4 additional hills beyond the colony count.
- **Hill radius:** 44–100, a maximum budget; only complete bands are fitted.
- **Contour band width:** 80–120% of `bestFarmRows(0)` (10 crop / 8 water corners).
- **Stairs per hill:** 2–4, each five corners wide.
- **Starting tower level:** 0–3; zero omits the towers.
- **Valley river:** on/off. A single hill has no inter-hill river.
- **Resource amounts:** all five ambient layers scale. Each occupied hill retains
  a small inner-terrace wheat patch beside every stair, one wood patch and one
  summit quarry at zero. The starter inn and tower reserves are also finite guarantees.

The supported envelope is geometric: the nearest pair of lattice hills and each
map side must fit two hills plus 14 corners of valley. A hill needs a radius-16
summit, two caps, and at least one complete band. Requests outside this envelope
are rejected before world mutation. Rectangles and odd colony counts use the same
fit rule. Starts are randomly dealt; this is comparable spacing, not exact symmetry.
At standard width, a band needs radius 38; two bands need radius 56. At 256×256,
four colonies normally fit two bands, whereas dense layouts fit fewer. A 64×64
map cannot fit the contract. Increasing the radius control only changes terrain
when an additional complete band fits inside the available spacing.

## Implementation and invariants

The generator reuses lattice sites, farm row fitting/planting, colony placement,
beaches, tower placement, resource guarantees and building-room helpers.
The shared farmland toolkit now provides `ContourFarmStyle` and `layContourFarm`
for capped contour rows and constant-width radial crossings, with `ContourWobble`
(three harmonics round the hill, own phases per hill, ramped in past the summit's cap
so the mapping along a ray stays monotone and no contour folds) making the hills lobed;
`contourNominal` is the radius the band arithmetic sees, which the generator uses to
tell hill ground from valley. The lobe amplitude is as much of four tiles as leaves
half the valley floor open at the closest pair of hills, so the fitted band count is
the same as for circles. `nearestTwoSites` in `Points`
provides exact owner/runner-up distances for valley boundaries, with stable ties and
explicit empty/single-site results. The optional `placeTower` coverage-point constraint selects a footprint that covers
every required approach tile, using the actual tower type's range. Empty coverage
points retain existing placement behavior. Its opt-in `supplyStone` fills the initial
reserve without requesting opening miners; other maps retain their existing stocks.
`plantPatchNear` combines bounded seed search and compact patch growth, returning
the actual count so required starter patches can reject insufficient room.
`placeStartingBuilding` shares the tower footprint search and provides selective
finite supplies, registering the new building's service lists without disturbing
existing colony tasks. Simulation rules retain their behavior. The source comments explain
corner-to-tile losses, containment, starter placement, radius quantization and
bounded tower searches.
The shared crop rescue now accepts an optional placement mask. Rice Terraces confines
its emergency wheat and wood to crop rows, including every tile of a radius-two clump.
When an extreme amount fills those rows with one crop, the rescue trades a small patch
of that accessible surplus for the missing starter crop. This keeps the town and stairs
dry while giving workers a reachable gathering edge. Other generators retain the
previous rescue policy unless they opt in to the mask.

Validation reconstructs the design and checks finished stair cores, full stair
reachability from each colony, inter-colony walking, summit building room, absence
of spreading summit deposits, and separation of summit and terrace pure-grass
components. Tower placement constrains the actual footprint to cover the full upper stair
width using the engine's firing range. Generation telemetry records fitted radii,
band widths/counts, stair angle/count, river water corners and crop budget saturation.

## Reproduction

```sh
scons release=1 server=0 -j6 map-generator-defaults-test map-generator-golden-test build/src/glob2
build/src/glob2 --generate-map rice-terraces --seed 7 --width 256 --height 256 --teams 4 --output artifacts/rice-terraces/seed-7.map --preview artifacts/rice-terraces/seed-7.png --json artifacts/rice-terraces/seed-7.json
python3 tools/map_telemetry.py collect --generators rice-terraces --seed-start 20001 --count 8 --set width=256 --set height=256 --set teams=4 --jobs 2 --out artifacts/rice-terraces/held-out
python3 tools/map_telemetry.py summarize artifacts/rice-terraces/held-out
```

[Verification results, previews, native maps, replays and study evidence](evidence/rice-terraces/README.md) are retained with this implementation.
The primitive regressions cover translated/wrapped contours, diagonal crossing cores,
future growth components, and nearest-site tie/sentinel behavior. The generator
regression includes resource extremes, rectangles, vacant hills and 12,000 unattended
resource-growth calls.

Static connectivity, generation repeatability and AI survival are separate checks;
human play is still needed to assess stair defense strength and valley incentives.

## Rotation tournament

A rotation tournament on revision 5 (six 256×256 maps, four colonies, every cyclic team
rotation, four Nicowars, 45,000 ticks) found the terraces even and hard: a root-mean-square
position bias of zero, pooled per-start peaks of 124 to 146 units, 20 to 34 warriors, 24
eliminations in 96 colony-games, and 60 to 67 starvation deaths per colony, the highest of any
map that still grows. The capped bands are worked (about 145 wheat harvested per colony), but
not fast enough for the armies the stairs invite, so the fights come with hunger. The defaults
stand; whether that pressure is the intended feel is a maintainer's judgment.

## Initial playtest tuning

Revision 4 retains three default stairs and the original terrain geometry. It supplies
starting tower reserves, distributes starter wheat near all inner stair mouths,
and gives each summit a small inn stocked only with wheat.
These changes target opening worker diversion and the first feeding deadline; they
do not alter AI or simulation rules. [Playtest results and evidence](evidence/rice-terraces/PLAYTEST.md)
compare the original, four-stair, reserve-only and nearby-food variants on paired
seeds, with prospective validation of the stocked-inn opening. The revision-1 archive above remains
available as the original implementation baseline.

## Bulk generation repair

Revision 5 keeps the same layout and opening supplies. A broad control study found
that the unconstrained crop rescue could put wood in a dry summit when dense wheat
made the original wood inaccessible. The shared placement mask and bounded crop
trade above repair that case. The original failed seed/settings requests, the
corrected reports, broad validation and generation timing are recorded in
[bulk generation evidence](evidence/rice-terraces/BULK_GENERATION.md). The AI
playtest archive describes revision 4 before this generation-only repair.
