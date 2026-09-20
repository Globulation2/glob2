# The Hungry Marches

`hungry-marches` (numeric ID 69, revision 1) is a landscape for fighting over food.
Each colony starts on dry ground with a finite wheat reserve. Renewable wheat
only grows in the shared floodplain. Outer districts offer alternative harvests;
the fragmented interior adds another front. Central concentration shifts more
of the renewable capacity toward that interior. It is not a promise that every
central district is richer than an outer farm; some seeds have small interior
banks whose strategic value is an alternative approach.

## Playing the map

Build an inn and establish shared harvesting before exhausting the opening
reserve. Defending the main swarm does not defend its food. Keep workers and
forward inns supplied, scout the alternative fields, and threaten an opponent's
harvest when their army commits elsewhere. Sand crossings and gaps between
wetland fingers allow flanking; an army cannot cover every field from one spot.

Wood starts near home and renews in separate floodplain bank sections. Quarries
are accessible near towns. Food is intended to be the defining shortage rather
than an inability to construct anything. Forward settlements and fortifications
are allowed; there are no ownership rules or scripted restrictions on buildings.

The generator targets free-for-all games with varied colony counts. Allied games
use ordinary alliance rules without special team placement or fixed roles. The
terrain is irregular, with comparable opportunities checked by actual walking
access; it does not promise exact symmetry or equal win rates.

## Controls

| Control | Meaning |
| --- | --- |
| Opening ration | Number of finite, dry wheat deposits per colony; normal engine deposit stock applies. |
| Central concentration | Shifts productive bank geometry toward the interior and away from outer districts. |
| Wheat amount | Initial planting density in shared fields, with a seed in every productive grain component. Does not add private renewable wheat or change eventual bank capacity. |
| Wood amount | Starter timber, renewable timber planting and dry scrub density. |
| Stone amount | Nearby quarry size, retaining the basic starting guarantee. |
| Algae amount | Algae seeded in suitable shared shallows. |

The opening ration ranges from 20 to 100 deposits (default 50), central
concentration from 50 to 80 (default 65), and resource amounts from 0% to 200%.
Zero resource settings retain essential starter supplies and grain seeds.

Supported sides are 128, 256 and 512 tiles, including rectangles. Games support
2–12 colonies, limited to four when either side is 128, and 1–8 starting workers.
Larger worlds retain bounded food journeys rather than scaling them with map
width. Higher colony counts add districts; low-population large worlds retain a
compact food front with more surrounding flanking ground. Unsupported crowded
combinations fail with a request diagnostic rather than silently removing the
food-access contract.

## Construction and invariants

The design is reconstructed from named seed streams. Wetland fingers use bent
local coordinates, uneven shorelines, sand caps, and paths crossing both water
and crops. Whole grass components determine timber versus grain banks, so trees
cannot grow into wheat. Sand caps are checked on the final rasterized map, not
only in the sketch.

Every private wheat deposit has zero growth probability under the engine's
actual fertility calculation. No ambient wheat placement, emergency pond repair
or no-growth flags are used. The final validator checks the requested private
ration, crop containment, timber separation, construction room and connected
colonies, including after banks fill with crops. Each colony needs two outer
food alternatives within 64 walking steps, each with at least 24 fertile grain
tiles and three units of summed growth potential. Every productive district must
be accessible to two colonies within 96 steps, with at most 40 steps between
their arrival distances (12 when the nearest is under eight). The access checks
cover initial wheat and mature banks. These bounds do not promise simultaneous
arrival or equal win rates.

Duel fields follow the actual homes' perpendicular bisector. With more colonies,
outer fields lie between neighbouring homes, with their grain banks facing both
approaches. Three-colony games use one central district to avoid swallowing the
outer alternatives. Crowded layouts move their outer fields inward to preserve
usable banks and dry starting supplies. Home-ring radii are capped at 116 tiles
to retain room for shoreline detours within the central walking budget.

## Verification

The [fixture guide](../../test/fixtures/hungry-marches/README.md) gives the control
studies, rotated AI games and profiling commands. Local evidence is indexed in
`artifacts/hungry-marches/VERIFICATION.md`, including completed saves and logs.

See `test/HungryMarchesContracts.h` for telemetry repeatability, supported shapes,
resource extremes, late growth and deliberate corruption checks. Reproduce with:

```sh
scons release=1 server=0 -j6 build/src/glob2 map-generator-defaults-test
build/src/MapGeneratorDefaultsTest hungry-marches-contracts --hungry-marches-only
build/src/glob2 --generate-map hungry-marches --seed 101 --width 256 --height 256 \
  --teams 4 --preview artifacts/hungry-marches.png --json artifacts/hungry-marches.json
```

This is an additive generator. It changes no simulation rules, save format,
replay acceptance or existing generator output. Maps and saves contain normal
terrain and resources. Human play remains necessary to judge the intended
raiding style; automated games provide economy and failure evidence.

### Known AI limitation

Maxima can stall at its initial population on a dry start. Its birth budget uses
fertility-weighted wheat, so finite opening stock contributes zero; the fallback
searches discovered ground and may not find the shared fields. In compact duel
tests it completed an inn but kept all birth ratios at zero in both positions.
This requires an AI planning fix, not private renewable food added to the map.
Use another AI or a human player when evaluating the intended opening.

Nicowar also has an existing full-roster bug: its Echo enemy iterator can run past
the twelve-entry team array when all slots are occupied. Twelve-colony automated
tests therefore use Cortex, Cabino and Maxima; avoid Nicowar at that count until
the iterator is fixed. This change does not modify AI or simulation code.
