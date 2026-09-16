# Vultures

**Play contract:** strip the dry wheat fields and attack before the finite food supply runs out.
Vultures uses Old Growth's sand-ringed home clearings and distant lakes. Wheat replaces the dry
forest in irregular fields; wood grows beside water. Homes retain building room, and starting
trails connect opponents without requiring swimming or an initial clearing campaign.

Every wheat tile starts with **three to five harvests**, drawn at random per tile from the map's
own `vultures-rations` stream once every repair is done, and has exactly zero growth probability
under the engine's water-and-sand rule. Until revision 4 (2026-09-16) every tile held one harvest,
which the engine draws as a nearly spent field; a maintainer's review asked for full-looking wheat,
so the food on a default map is about four times what the playtests below measured. This includes the starting rations. There are no renewable wheat
plots, fruit or algae. Wheat therefore cannot spread into irrigated ground: no initial wheat
source can grow. Wood starts within eight tiles (wrapped Chebyshev distance) of pure water;
normal shoreline wood regrowth remains enabled. No simulation rules or tile growth flags change.

Each colony receives 48 dry wheat tiles, 24 shoreline wood tiles and a five-tile quarry before
trail and room clearing. The finished-world validator checks reachable wheat within 24 steps,
wood within 32, at least 16 reachable overlapping 4×4 building origins within 24 steps, and a
walking route between all colonies. These establish an opening, not indefinite survival.

## Controls

- **Wheat amount:** scales the default 65% coverage of eligible dry ground outside home clearings
  (full cover from about 155%). Every tile holds three to five harvests. At zero, only starting rations remain.
- **Wood amount:** scales the default 20% scatter chance on eligible ground within eight tiles
  of water. At zero, the starter wood remains.
- **Home size:** requested radius 16–30, default 24. Homes shrink when needed to preserve the
  intervening fields, using the same spacing rule as Old Growth.
- **Lakes / Lake size:** zero to four requested lakes per 128×128 area, of 40–160 water corners.
  Defaults are one and 90. Lakes that cannot fit the clearance budget are omitted and reported
  in telemetry. More lakes add timber sites and reduce the finite dry wheat area.

Map dimensions use the shared 64–512 power-of-two controls, including rectangles. Colony counts
must fit the shared clearing geometry; excessive counts receive an explicit rejection. The
vacancy fallback rescues some awkward seven- and eleven-colony rectangles but cannot fit
colonies onto genuinely cramped 64-tile shapes. Seed
variation changes the fields, shorelines, lake placement and assignment of colonies to homes.
Asymmetric layouts are not claimed to be exactly fair. The candidate scorer gives fertility
zero weight because renewable food is deliberately absent.

## Implementation

`ClearingLandscape` shares Old Growth's layout construction, preserving its arithmetic, seed
streams and traversal order for already playable requests. Vultures opts into a vacancy
fallback only when the ordinary clearing geometry cannot fit; Old Growth does not. Vultures
has its own furnishing and final validation. Common crop
repairs are deliberately avoided because they could insert renewable wheat; trail and room
repairs can only remove resources. Numeric ID 47 is additive; save, replay and network formats
are unchanged. Revision 2 records the geometry fallback; its previously playable golden
fingerprints are unchanged. Generated maps use the existing full-world serializer.

Telemetry records home geometry, requested/placed lakes, starting deposits, eligible dry field
area, effective coverage, ambient deposits and the final total number of food rations. Default
play is intended to put a deadline on growth and military investment. Large maps and high wheat
amounts lengthen that deadline. Human play is needed to judge that pacing.

### Initial AI playtest and field tuning

A retained Linux x86_64 pilot used 128×128, two-colony maps with map seeds 7 and 11, game seed
19, no candidate selection, two map rotations, and a 32,000-tick cap. Four Nicowar mirrors at
the 100% wheat default made warriors early and first fought between ticks 6,656 and 10,752.
One reached a military win at tick 27,010; the other three were still active at the cap, with
the stronger colonies holding 59–61 warriors. Hungry units and starvation deaths appeared
despite unharvested wheat remaining on the map. The pressure is therefore partly **access and
delivery time**, not merely the total number of wheat tiles. A Cortex mirror built and trained
more slowly, with first recorded combat at tick 30,208. A Maxima mirror stayed at four workers
and three buildings per side, without an army. These are AI behaviors observed on this finite-
food scenario, not final judgments on human pacing or every map size.

The tuning probe raised the existing **Wheat amount** control to 125%, increasing initial wheat
from 3,347 to 4,182 rations on each sampled Linux map (revision 3); each tile still held one harvest with
zero fertility. On seed 7 one Nicowar rotation won earlier, while the other fought past the cap.
On seed 11 the 100% opening won in one rotation but both 125% openings remained active at the
cap. Hunger and starvation moved in both directions, and Maxima still made no army. Because
this small paired test found no reliable improvement and extra food can soften the requested
deadline, that probe left **35% base field cover** in place. Maintainer review of the picture
then asked for the fields filled in, with passages cleared by workers rather than found: the
base cover is now **65%**, the open ground the exception rather than the rule, and the trails
the only routes that come cleared. The control still runs 0 to 200%. The raw map, save, result, and gameplay telemetry records
are retained in [the playtest evidence](../artifacts/vultures/PLAYTEST.md).

### Bulk generation and rectangular tuning

The Linux bulk job replayed 461 requests: a full 64–512 width/height by one-to-twelve-colony
envelope (193 jobs including a baseline) and 268 cases covering every registered control
value, compact corners, extreme abundance and seeded mixed settings. Revision 1 completed
398/461; its 63 other results were explicit geometry rejections, not crashes or partial
worlds. Eleven-colony prime layouts and a seven-colony 256×128 rectangle were rejected even
when a larger composite colony count could fit. The shared opt-in `roomyLatticeSites`
fallback tests up to four extra sites, removes the surplus sites by maximizing minimum wrapped
whole-tile spacing, and is accepted only if the resulting radius passes the existing home-room
rule. It draws no additional random values and never replaces an already valid layout.

Revision 2 completed **406/461** with **55 clean geometry rejections**, zero generation
failures, crashes, timeouts or missing reports. Eight formerly rejected cases became playable;
all 398 previously successful cases still completed. In the complete parameter sweep,
**268/268** completed after the repair. Shapes too small for a pond, swarm and separate
homes still reject honestly, especially 64-tile squares with multiple colonies. The full
case list, reports, telemetry, transition table and reproduction runner are retained in
[bulk validation evidence](../artifacts/vultures/BULK.md). No control range was narrowed
to hide difficult maps.

## Generate a playable example

```sh
scons release=1 server=0 -j6 build/src/glob2
GLOB2_USER_DIR=/tmp/glob2-vultures-profile build/src/glob2 --generate-map vultures \
  --seed 7 --width 256 --height 256 --teams 4 \
  --output artifacts/vultures/vultures-7.map \
  --preview artifacts/vultures/vultures-7.png \
  --json artifacts/vultures/vultures-7.json
```

Verification results and retained examples are recorded in
[the evidence notes](../artifacts/vultures/README.md).

## Budgets and tuning rationale

These numbers define a scenario, not universal engine balance rules. They are named in
`VulturesGenerator.cpp`; the shared geometry retains Old Growth's construction values.

| Budget / heuristic | Reason and limitation |
| --- | --- |
| 48 starter wheat tiles, three to five harvests each | A finite bridge to exterior foraging. Kept at zero abundance. Actual survival depends on staffing, travel and population decisions. |
| 24 starter wood tiles | Construction must not depend on a lucky ambient scatter. Normal engine stock amounts and regrowth remain. |
| Five quarry tiles | Permanent mining frontage for upgrades and ammunition. Stone is eternal, so this does not describe five pieces of stone. A partial quarry fails generation. |
| 65% exterior wheat cover at 100% abundance | The fields are the ground and the pockets the exception, so moving off the trails means harvesting a way through; the first calibration's 35% left the plain mostly open. Quantile ties can change the exact fraction; trails can remove more. Cover is capped at 100% from about 155% abundance. |
| Five noise octaves | Patches vary at several scales; isolated random speckles would give many trivial paths and lose Old Growth's field-clearing feel. |
| 20% shoreline wood scatter | Leaves initial gathering gaps. Scales to 40% at maximum abundance. Later regrowth can fill them; resource scarcity and town management remain player concerns. |
| Eight-tile shore band | A visibly local wood supply, within the 15-tile growth-probe reach. Measured from final pure-water tiles using wrapped Chebyshev distance. This is an initial placement constraint, not a new simulation boundary. |
| Pond search radius 12, quarry offset nine | Search across the beach to actual grass; place the quarry past the pond to separate it from the swarm and food patch. Full patch budgets are checked rather than assuming a seed guarantees room. |
| Wheat reach 24, wood reach 32 | Standard framework opening targets. Final access follows worker paths and counts a final step to harvest from an adjacent tile. A nearby but inaccessible crop does not pass. |
| 16 building origins within 24 steps | Standard minimum room check, after furnishing and repairs. Origins overlap. Wider effective town space is inspected in previews and games. |
| Route costs 1 open / 3 clearable | Prefer gaps, but allow a short cut through a field instead of a large detour. These are relative route costs, not simulation ticks. Water, buildings, fruit and stone cannot be crossed. |
| Candidate weights 30/25/0/20/15/10 | Wheat access, wood access, fertility, stock depth, building room and rival distance respectively. Zero fertility weight is essential for a deliberately finite-food scenario. This score is a seed-selection heuristic, not proof of fairness. |

### Shared geometry numbers

Homes use Old Growth's roomiest lattice, dealt randomly to team indices. Their effective radius
is the smaller of the requested radius and `floor(spacing / 3 - 3 - 2)`: one-third of spacing,
minus a three-tile clearing margin and two-tile sand containment ring. If the shared pond/swarm
room test fails, Vultures alone tries the opt-in vacancy lattice before rejecting. The fallback
compares the ordinary team-count lattice with grids of one to four extra sites. At each removal,
it picks the site whose omission gives the largest minimum wrapped whole-tile separation;
ties keep the first site in lattice order. It accepts only a strict spacing improvement that
passes the same `homeHasRoom` radius rule. Four extra sites cover the difficult 8–11 counts
through the 12-site composite grid and cap the search cost. Degenerate dimensions return an
empty result before row arithmetic, and a caller's larger vacancy limit is clamped to four.
The sand ring prevents shoreline wood from spreading
across the entire home boundary into the exterior fields.

The home outline uses 15% radial variation; its central pond uses 30%. Their shape and placement
are identical to the existing shared Old Growth design. Optional distant lakes keep 20 tiles
from settled clearings and other lake shores: this separates the lake's 15-tile irrigation reach
from homes and makes lakes read as distinct landmarks. Lake count scales with area/16384
(128×128), rounded to the nearest integer. A seed is chosen at greatest remaining clearance;
water grows by distance priority in thousandths of a tile, perturbed by up to 2.5 tiles of
periodic noise. The noise priority uses `2500 / 65536` because its samples span 0–65535.
Existing arithmetic and stream names are preserved for Old Growth compatibility.

### Fallback and error policy

- Shrinking homes and omitting lakes are the shared layout's explicit, telemetered fallbacks.
  Vultures requests no peripheral home pools because they would eliminate dry starting ground.
- Only a crowded, failing Vultures layout invokes the vacancy search. A passing result records
  both its count and a fallback message; if none passes the existing room rule, the request
  remains an explicit geometry rejection. Old Growth never invokes it.
- A kit search may leave the home outline to find dry wheat, but never ignores fertility,
  swarm clearance, occupancy or terrain. The finished world must still meet walking limits.
- Missing or partial wheat, wood or quarry kits fail the candidate. They are never repaired by
  silently planting wet wheat or remote wood. Failed partially populated worlds are discarded.
- Trail carving and cramped-start relief may remove resources. A fresh final audit checks
  stock, dry wheat, shore-only wood, required supply access, building room and colony contact.
- The CLI reports failure for that exact request/seed. The lobby's existing candidate mechanism
  may try another recorded seed; no generator loop depends on time or changes settings secretly.
- Invalid calls to shared stock-cap and access-validation operations throw `GenerationFailure`.
  These catch programmer mistakes; they are distinct from a valid request that cannot place its
  promised world. Failures retain the generation stage and collected telemetry.

## Rotation tournament

A rotation tournament (six 256×256 maps, four colonies, every cyclic team rotation, four
Nicowars, 45,000 ticks) found the map even and lively: pooled per-start peaks of 125 to 131
units, 50 to 55 warriors per colony, five eliminations in 96 colony-games, and a
root-mean-square position bias of zero. Starvation deaths of 20 to 25 per colony are the
finite food doing its work rather than a defect. No colony builds prestige on a finite-food
map, so games that reach the cap are adjudicated on units, which makes the team-index tally of
that adjudication noisy; read the per-start economy instead. The defaults are unchanged.

## Reusable framework additions

- `ClearingLandscape`: layout construction extracted unchanged from Old Growth. Vultures supplies
  its home/lake settings and telemetry namespace; the resource policy stays outside the primitive.
- `pureTiles(Map, TerrainType)`: finished-map counterpart to `pureTiles(TerrainSketch, Torus, type)`.
  Mixed beach tiles are excluded. This supports final-world habitat validation after repairs.
- `capResourceStock`: caps existing stocks without refilling, creating deposits, changing sprites
  or drawing RNG. Returns tile count and remaining stock from the same pass for telemetry. It
  does not disable growth; callers must establish dryness separately. Vultures capped its wheat
  at one harvest with it until revision 4 and now sets three to five per tile itself; the
  primitive stays for a finite crop that wants a cap.
- `startingAccessFailure`: read-only supply and room validator. Callers supply resource types,
  names and travel budgets; it supports stone or fruit targets as well as wheat and wood. It
  floods from actual workers with the engine's non-swimmer predicate and reports the first
  unmet rule with colony and observed distance/site count. It never plants a repair resource.

Existing callers retain their existing repair behavior. Only Old Growth is redirected through
an extracted implementation, so its golden fingerprints must remain unchanged.

### Tuning from retained failures

The first 256×128, five-colony sweep (seeds 201–204, home size 30, no lakes) exposed a
placement heuristic problem. Shared spacing shrank the effective home radius to 12. The nearest
dry pocket held only ten tiles, so a single `growPatch` returned ten and the candidate failed
even though other nearby dry ground was available.

The reusable response is **`growPatchesNear`**, not a larger water allowance or a smaller food
budget. It grows the nearest eligible patch, then the next nearest eligible patch until the
budget is filled or eligible seeds are exhausted. Existing deposits and terrain/occupancy the
engine disallows are excluded internally. Each successful iteration places at least one tile,
so the requested tile budget bounds the iterations. It returns both tiles and patch count;
Vultures records a per-colony `split-rations` fallback when more than one patch is needed.
Seed searches stay within the requested box, but patch growth can extend outside it wherever
the predicate permits; the final walking audit remains mandatory. A dedicated disconnected-
pocket fixture verifies both complete placement and termination when the habitat is exhausted.

This keeps placement mechanics general and leaves only the habitat choice and opening budget
in Vultures. The original failed requests are retained alongside the corrected sweep.

Retesting that rectangle fixed all ration shortfalls and exposed a second instance of the same
problem: seed 202's fourth colony had a one-tile nearest quarry pocket. Wood and quarry budgets
now use the same shared operation as wheat. Per-colony patch counts and `split-wood` /
`split-quarry` fallbacks expose that construction. The four retained seeds are also exercised
by the existing CI-wired defaults harness.

The JSON report's `canonical_quality` intentionally retains the common cross-generator weights;
it is not Vultures' candidate-selection score. Use the raw supply/room/contact measurements and
the generator's own weights when interpreting this deliberately dry scenario.

### Revision 3 on the same protocol

The same six maps, rotations and AIs on revision 3 (65% base cover): pooled per-start peaks of 162
to 170 units against 126 to 131 on revision 2, 89 to 96 warriors against 50 to 55, 17 to 19
starvation deaths per colony against 20 to 25, no elimination in 24 games against five, every game
at the tick cap, a root-mean-square position bias of zero. Wheat harvested per colony fell from 151
to 127: with the fields filled in the colonies clear less and hold more, feed better and arm more,
and nobody is overrun by the cap. The fuller fields make the map more even and better fed, not
hungrier; whether a human wants the 35% openness back is a control away.
