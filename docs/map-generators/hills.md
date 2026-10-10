# Hills

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
Every stair carries on past the outer cap as a three-corner sand road across the valley to the
edge of the hill's ground (where another hill becomes the nearest) and four tiles past it, so it
fords the valley river and connects each stair to the neighboring valley. Between every two stairs, just beyond the outer
cap's furthest lobe, a small wild patch of four crop tiles alternates wheat and wood. They lie within the
growth probe of the hill's outer water band and spread into the commons over a long game; the
amounts scale them to nothing.
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
The shared crop rescue now accepts an optional placement mask. Hills confines
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
scons release=1 server=0 -j6 engine-tests map-generator-golden-test build/src/glob2
build/src/glob2 map generate hills --seed 7 --width 256 --height 256 --teams 4 --output artifacts/hills/seed-7.map --preview artifacts/hills/seed-7.png --report-file artifacts/hills/seed-7.json
python3 tools/map_telemetry.py collect --generators hills --seed-start 20001 --count 8 --set width=256 --set height=256 --set teams=4 --jobs 2 --output-dir artifacts/hills/held-out
python3 tools/map_telemetry.py summarize artifacts/hills/held-out
```

Keep verification results, previews, native maps, replays and study evidence in
the ignored `artifacts/hills/` workspace or in pull-request attachments.
The primitive regressions cover translated/wrapped contours, diagonal crossing cores,
future growth components, and nearest-site tie/sentinel behavior. The generator
regression includes resource extremes, rectangles, vacant hills and 12,000 unattended
resource-growth calls.

Static connectivity, generation repeatability and AI survival are separate checks;
human play is still needed to assess stair defense strength and valley incentives.

## Playtest limitations

Check retained crossings, town room and renewable resources on both rectangles and crowded requests. Generation success alone does not verify usable late routes.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[HillsGenerator.cpp](../../src/map/generator/generators/HillsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
