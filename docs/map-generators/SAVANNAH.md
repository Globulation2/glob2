# Savannah

Savannah (`savannah`, numeric ID **33**, revision **1**) is mostly open grassland.
Colony economies begin beside small home ponds. Larger neutral watering holes offer
renewable crops and three fruit kinds, with open ground for forward inns. Scattered
wood groves and small quarries punctuate the plains. The intended choice is whether
to expand toward a watering hole or use the open land to flank an opponent.

This is an asymmetric landscape with equal starter floors. It does not promise
identical travel costs, exact resource yields, or equal chances of winning.
Human play is needed to judge whether its ponds actually attract conflict.

## Controls and supported requests

| Control | Values | Default | Meaning |
| --- | --- | --- | --- |
| Watering holes | Sparse / Normal / Many | Normal | Targets 1 / 2 / 3 **neutral** ponds per 128×128 area, in addition to home ponds. |
| Dry patches | 0–20%, step 1 | 8% | Share of eligible plains vertices converted to sand, outside home, approach and feature reservations. This excludes structural sand margins and beaches. |
| Wheat, wood, stone, algae, fruit amounts | 0–300%, step 25 | 100% | Additional deposits; exact tile counts saturate when eligible plots fill. |

Sides are existing power-of-two sizes **128, 256, 512**, square or rectangles up to
**2:1**. At least **4,096 tiles per colony** and **48 tiles between home centres**
are required. The engine currently supports at most 12 colonies. Requests failing either actual spacing or the essential-feature fit
are rejected with an actionable error. Homes never shrink to accept crowding.
The generic controls retain their existing ranges for other generators.

Zero abundance retains each colony's pond, **20 renewable wheat tiles, 20 renewable
wood tiles and four stone tiles**. All other resources respond to their controls.
Ponds, plot outlines and sand margins remain at zero. Algae has no starter floor.

## Geometry budget and numerical choices

These are design heuristics, except where an engine rule is identified. They are
not universal survival thresholds. The comments beside the constants and placement
operations explain their units; the evidence section records actual verification.

- **Homes:** radius 20 with an 8% radial ripple for ownership; a radius-23 reservation
  keeps decoration off their margins. Sites come from the roomiest existing lattice,
  receive at most four tiles of jitter per axis, then are randomly dealt to colonies.
  A proposed move that breaks 48-tile spacing leaves that site in place. One proposal
  per site keeps runtime bounded; this is not a search for an optimal layout.
- **Town:** the swarm lies six tiles west of home centre, from the shared `Homes`
  calculation. The eastern half carries food, wood and water. The western half stays
  available for ordinary buildings and upgrades. The final check requires at least
  16 reachable **8×8** anchors within 24 walking steps, each large enough for a 4×4
  building with two tiles of margin. Anchors overlap; this does not mean 16 separate
  buildings fit.
- **Approaches:** two opposed five-tile strips run north and south from the western
  town side, 5–24 tiles from home centre. Reservations extend two tiles farther and
  have two extra tiles on either side to absorb sand-corner effects. Final checks
  also require both ends to join the same five-tile-wide plains network: erode the
  walkable mask by two and require cardinal connectivity of its remaining core. They
  require every strip tile to be walkable and reachable from the colony's workers.
  Moving units do not count as permanent route obstacles.
- **Home crops:** wheat has radius 6.5 at (+6, −11), wood radius five at (+6, +10),
  both with 18% ripple. The pond is at (+9, 0), radius 4.5 with 25% ripple. Crop
  planting ranks exact engine fertility; dry tiles cannot count toward starter
  floors. At 100%, wheat has 20 guaranteed plus 20 scaled initial deposits; wood
  has 20 guaranteed plus eight scaled. The first calibration used radius-four
  wheat, eight scaled deposits and a radius-3.5 pond. Long games exposed food
  pressure, prompting the 40-tile wheat start within the same home reservation.
  Later four-colony games reduced 236 global initial wheat tiles to 66/93 at
  tick 24,000. The final tune enlarges *unplanted* crop growth room while leaving
  those 40/28 initial counts unchanged. It extends both plots away from town:
  the wheat western cap stays at approximately `5 − 5.5 − 2 = 6 − 6.5 − 2 = −2.5`
  local x, and the wood cap at `5 − 4 − 2 = 6 − 5 − 2 = −1`. The two-corner sand
  stencil and radial roughness make these nominal bounds approximate on final tiles.
  A rejected tune shifted the wheat cap toward town and planted 50 initial tiles;
  Numbi then harvested zero wheat on both test seeds and starved before combat.
  Preserving nearby food-inn space is therefore a play constraint, even when static
  gathering paths say more wheat is reachable. Both final plots fit the radius-23
  reservation and leave the western five-tile approaches clear.
  The radius-two quarry sits 18 tiles east, leaving its grass beyond the pond’s
  beach and its sand cap within the home reservation.
- **Algae:** one radius-one clump per 30 eligible pure-water tiles at 100%,
  scaled by abundance using the existing fertility-aware `seedAlgae` operation.
  It never creates water or changes land connectivity.
- **Neutral ponds:** radius 4.5, 25% ripple; north/south crop plots radius five,
  offset ten tiles, with targets 38 wheat and 28 wood at 100%. Three radius-two
  fruit plots lie on the east side, seven tiles apart, with two deposits of each
  kind at 100%. The west in this local frame remains clear for a forward inn and its margins.
  Each neutral module receives a separately seeded heading, so these directions
  rotate across the map in seeded tenth-degree increments (3,600 possible headings).
- **Neutral placement:** enumerate every integer centre whose shared Euclidean
  distance transform clears the home and approach reservations. Shuffle eligible
  centres, then stable-sort by summed squared distance to the two closest homes
  (one home for solo maps). This favours neighbouring frontiers without targeting
  the map centre. Stable ties retain the seeded shuffle order. An earlier budget
  of 1,600 darts per 128-square area missed a narrow legal gap at seed 1001 on a
  128×128 four-colony map; exhaustive eligibility removes that sampling failure
  without shrinking homes or rerolling the seed. A radius-19 feature reservation plus a one-tile gap
  must fit clear of every home, approach and previously accepted feature.
- **Plains:** shared dart throwing with spacing 22 proposes small groves. An 8-tile
  clearance box must be free before a feature is accepted; radius seven is reserved
  afterwards. Four of every five accepted features are radius-three wood groves
  with seven tiles at 100%; the fifth is a radius-two quarry with two stone tiles.
  These optional wood deposits may be finite dry reserves. Home wood is renewable.
- **Dry patches:** periodic noise with cell period 14 makes coherent patches instead
  of speckles. `sprinkleSand` converts the highest-ranked eligible vertices and
  keeps three steps from water. Beaches are laid after all terrain operations.
  Plot eligibility is then trimmed to final pure-grass tiles; beaches may remove
  pond-facing edge tiles, but starter floors are never reduced.

## Reusable framework operations

The change adds operations to existing modules, with no changes to existing defaults:

- `Orbits::jitterSites`: bounded integer proposals on a torus, accepted only while
  preserving caller-supplied minimum spacing. Returns the accepted count.
- `Farmland::stampContainedPlot`: arbitrary grass-corner sets surrounded by a
  two-corner-wide square-stencil sand margin. Returns the actual pure-grass tile
  list, including across map seams. The caller reserves enough space first;
  stamping deliberately overwrites its footprint.
- `Farmland::plantContainedPlot`: deterministic fertility ranking within an explicit
  tile list, optional renewable-only eligibility, a hard count ceiling, and an
  actual planted count. It excludes occupied tiles and supports finite groves and
  permanent quarries as well as crops.
- `Farmland::containedPlotsMismatch`: checks final grass adjacency against plot labels
  and rejects spreading crops planted outside those labels. Any eight-neighbour
  grass connection out of a plot or into another plot fails.

The engine expands wheat and wood only onto neighbouring grass. The final adjacency
check therefore proves that these crops cannot spread out of their plots under
ordinary resource growth, for any number of ticks. It also keeps wood from invading
wheat. This proof does not cover an editor changing terrain or a player deliberately
planting resources outside the designed plots. No tile growth flags, growth mechanics,
simulation scheduling, save format or replay/network versions change.

## Stages, error handling and fallbacks

`design(request, context)` reconstructs all terrain, homes and plot labels using
named streams for home layout, offsets, outlines, pond sites, pond outlines, groves
and dry patches. Resource percentages are read only during furnishing. Validation
uses a fresh context, so reconstruction cannot advance generation's random streams.

Generation writes terrain, creates teams, places swarms/workers on home grass outside
crop plots, then plants resources and algae. The finished-world validator runs after
all those mutations. It checks dimensions, retained pure water at every pond, crop
containment, renewable gathering access (wheat within 24 steps, wood within 32),
reachable construction margins, forward-inn room, both approaches, a five-tile-wide
shared plains network and colony-to-colony land contact.

Optional ponds may be omitted when they cannot fit. **At least one neutral pond is
essential**; if none fits, the request fails. Optional grove candidates are skipped
when crowded. Resource targets saturate inside their plots. An unmet starter floor
fails generation instead of scattering emergency resources into the town.

There is deliberately no unrestricted crop top-up or route-clearing repair. Those
operations could break the containment contract or conceal a layout error. A failed
candidate remains disposable under `GenerationService`'s existing failure contract.
Error messages suggest a larger map or fewer colonies where geometry is the cause.

## Telemetry

All counts are observations of work already performed; no RNG draws or additional
map analysis are added when telemetry is enabled.

| Key | Meaning |
| --- | --- |
| `savannah.homes.jitter-accepted` | Site proposals satisfying the spacing constraint. |
| `savannah.homes.spacing` | Minimum wrapped home-centre distance, tiles. |
| `savannah.ponds.requested`, `.placed` | Neutral pond targets and accepted features. |
| `savannah.ponds.eligible-centres` | Integer centres clearing the initial home/approach reservations. |
| `savannah.ponds.omitted` | Actual omission due to exhausted reserved space. |
| `savannah.plains.clumps`, `.proposed-clumps` | Accepted and proposed optional grove/outcrop sites. |
| `savannah.plains.omitted-clumps` | Candidate clumps intersected existing reservations. |
| `savannah.plots.beach-trimmed-tiles` | Pre-beach plot tiles excluded from final crop eligibility. |
| `savannah.plot.requested`, `.planted` | Deposit tile targets and placements, subject = plot index. |
| `savannah.plot.saturated` | A plot exhausted eligible tiles before its target. |

Saturation at high abundance is expected, not a repair. Count omissions per attempted
map and inspect the final-map metrics alongside them; multiple plot records on one
map must not be treated as independent maps. Final reports also retain the complete
request and generator revision.

## Verification and review

### Rotation tournament

A rotation tournament (six 256×256 maps, four colonies, every cyclic team rotation, four
Nicowars, 45,000 ticks) found every colony on every map flat at 22 to 33 units for the whole
game, with 11 to 15 buildings, four starvation deaths and a dozen births: a colony that builds
but never breeds. The gameplay telemetry explains it: 55 to 96 wheat harvested per colony in
45,000 ticks against 300 wood, about sixty new wheat tiles grown within eight tiles of the
colony's buildings in that time, and 650 new wheat tiles on the whole map. Enlarging the home
pond from radius 4.5 to 6.5 (roughly doubling the water the growth probe can find from the
plot) was tried on the same six seeds and changed nothing (peaks 27 to 29, wheat harvested 85),
so the pond is not the limit either: a Nicowar colony harvests almost nothing from the ringed
plot however fast it regrows. The defaults stand unchanged; the finding is that this map's
sealed-plot economy needs a farmer that will work it, which the current AIs are not, and human
play is the test that remains.


The existing CI defaults harness includes seam containment, deliberately broken
containment, fertility filtering, planting limits and repeatable jitter checks.
Savannah contracts cover supported rectangles, odd/solo colonies, invalid dimensions
and crowding, zero/maximum terrain stability, and 4,096 unattended engine growth calls.
The all-generator golden and telemetry harnesses cover existing helper callers.

Review evidence and measured tuning results are on the [evidence/savannah branch](https://github.com/Globulation2/glob2/blob/evidence/savannah/artifacts/savannah/SUMMARY.md); the sample previews are under `docs/artifacts/savannah/`.
See that run's summary for exact commands, seeds, platform, generated maps, previews,
telemetry, save/reload and populated-game coverage. Generation success and static
room checks do not establish enjoyable pacing, conflict around ponds or useful
flanking. Human play remains a separate acceptance step.

### Initial implementation baseline (macOS arm64)

The pre-playtest calibration passed **401 map cases**: 32 training seeds and 32 fresh
held-out seeds at four representative settings (256 cases), plus 145 parameter
cases. The separate generator sweep passed 46/46 cases. All 96 all-generator
telemetry comparisons passed, and the golden update added eight Savannah rows
without changing existing generator hashes. The defaults and setup harnesses passed,
including 4,096 unattended growth calls. A production save/reload retained terrain,
resources, movement metrics and preview pixels; telemetry on/off maps were byte-identical.

Six populated mirror games on that baseline exercised Nicowar, Numbi and Maxima with two starting-seat
rotations, up to 60,000 ticks or engine termination. All three AIs expanded and dealt
combat damage. Numbi had no starvation deaths; Nicowar and Maxima still experienced
food pressure, including during combat. Larger starter wheat plots increased capacity
but did not uniformly eliminate starvation. These are play observations, not proof
of balance. Human play, pond-driven conflict, useful flanking and Windows execution
remain unverified. No simulation or compatibility version
changes were necessary.

### Light populated-game tune (macOS arm64 and Linux x86_64)

The final home plots were then checked in two matched 24,000-tick mixed-AI games,
using map seeds 1 and 2, game seed 23, and fixed seats. Their initial resource
counts stayed at 236 wheat tiles and 168/182 wood tiles. At the cap, global wheat
remained 80/104 tiles (baseline 66/93), and starvation deaths totalled 7/12
(baseline 10/23). Wood remained 63/45 tiles (baseline 82/33), so the wood result
is mixed. All four games reached combat. Changed terrain changes AI decisions
and fighting, so these differences are evidence on two maps, not a controlled
measure of a general win-rate or food-production gain.

An initial attempted enlargement planted 50 wheat per home and shifted its sand
edge toward town. Numbi then harvested no wheat on both maps and began starving
around tick 5,120. The final tune kept the original initial counts and town-facing
edges; Numbi harvested wheat early in both matched games. A separate 24,000-tick
four-colony game on `pharaoh-dev-3.local` with a new map seed 3005 completed with
combat and 37 starvation deaths, predominantly in the two Maxima seats. This is a
material remaining balance limit, not an omitted failure.

The final defaults harness (including 4,096 unattended growth calls), 46/46
Savannah sweep, optimized macOS/Linux builds and eight Savannah golden rows passed.
Existing generator golden rows stayed unchanged. The Linux generator also completed
seed 3005; its tile layout differed from macOS for the same seed, while loading the
saved Linux map on macOS preserved terrain, resources, fertility, space, starts and
movement reports. Linux game saves loaded on macOS. The post-tune 401-case matrix,
Windows run, per-tick cross-platform checksums and human play were not performed.
No simulation, save-format, replay or network version changed.

[The playtest summary](https://github.com/Globulation2/glob2/blob/evidence/savannah/artifacts/savannah/playtest-round1/SUMMARY.md) records the requests, per-team
telemetry, preview images, candidate failures, saves, replays and commands.

### Bulk supported-envelope audit (Linux x86_64)

A later 130-case stress pilot and **742-case fresh-seed bulk matrix** generated
every valid request successfully. The full matrix exercised all 21 dry values
against all three pond choices on the smallest crowded square and both 2:1
rectangle orientations; all 13 legal values of each existing resource slider;
and 240 independent mixed-control requests across 15 legal size/colony shapes.
The smallest 128×128 square was tested with one, three and four colonies, and
both 256×128 and 128×256 rectangles with up to eight. There were no generation
failures or maps without the essential neutral pond. The observed worst wheat
and wood gathering distance was 14 walking steps; weakest-home reachable 4×4
placement origins never fell below 745.

The bulk requests targeted 4,092 neutral ponds and placed 4,021. **55/742 maps**
omitted at least one optional pond target, mainly crowded Many requests; all
Sparse targets were placed. The 128×128 four-colony Many control placed 63/81
target ponds across 27 maps and could fall to one neutral pond on a map. High
abundance saturated at least one contained plot on 264 maps, but none of the
292 all-default-abundance maps or 15 all-zero maps saturated. This is the
documented finite-plot ceiling rather than a failed starter guarantee.

No parameter range or geometry budget changed after the bulk audit: the tested
valid envelope succeeded with accessible starts and room, and optional feature
omissions did not compromise the required routes. The reviewer evidence under
[The bulk summary](https://github.com/Globulation2/glob2/blob/evidence/savannah/artifacts/savannah/bulk/SUMMARY.md) retains every request/report, failure-rate
denominator, feature counts, timing, eight saved sample maps with previews and
the exact commands. The matrix samples mixed sliders but cannot exhaust all
13⁵ resource combinations; it ran on Linux x86_64, with the earlier macOS
verification separate. Windows and human play remain unverified. All 32 shipped
non-English language tables contain local Savannah labels; the catalog audit reports
zero missing or untranslated keys, and the English-fallback regression test passes.
Identical native spellings of the Normal choice in ten languages are recorded as
reviewed shared vocabulary.
