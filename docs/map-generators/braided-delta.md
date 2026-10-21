# Braided Delta

## Play contract and construction

1. **Budget islands first.** Divide the short map dimension among the requested
   braids. Each lane needs at least `island-size + 16` tiles: the requested land
   budget plus channels, beaches and rasterization margin. Rejoining intervals
   never fall below `max(64, 2 * island-size)` tiles. Reject combinations that
   cannot fit, rather than shrinking colonies or filling channels as a repair.
2. **Close loops on a torus.** Rivers run along the longer dimension; a square
   map randomly chooses horizontal or vertical flow. Shared periodic meanders
   preserve separation and join smoothly across the seam. Side channels shift
   downstream by 45% of an island interval, easing into the adjoining rivers.
   Stagger their attachment stations between lanes so there is no common hub.
3. **Connect the banks.** Propose a ford midway along each side channel and
   regularly along each main channel, with seeded offsets. After beaches, move
   junction-adjacent candidates up to 16 tiles to find two pure-grass endpoints.
   Omit candidates that still cannot fit; never turn a whole reach into a bridge.
   Sand crossings are about four corners wide, providing room for traffic and
   resisting crop spread. Final validation requires every island clearing to be
   reachable, including neutral ones.
4. **Keep useful interiors.** Before furnishing, reserve a 20×20 pure-grass town
   on every island, 25 tiles from its upstream channel. Check its entire footprint
   and surrounding margin before stamping it. If a meander clips a corner, search
   outward in two-tile steps, moving the entire clearing at most six tiles on each
   axis. Never shrink it or fill river water. A two-vertex sand rim separates
   it from spreading crops. Neutral clearings are expansion sites as well as
   alternative colony seats. This regular clearing shape deliberately trades
   some natural appearance for reliable construction and growth containment.
5. **Spread the starts.** Choose island seats by farthest-point selection after
   shuffling candidates, then randomly deal selected sites to teams. One colony
   occupies each chosen island. This reduces crowding and persistent team-index
   bias; it does not establish equal walking distances or competitive balance.
6. **Allocate resources by island.** Place home supplies first: a 48-tile wheat
   budget and 32-tile timber budget beside the upstream bank, splitting across
   pockets if a sand approach interrupts the ground. These opening supplies
   remain at zero resource amounts. Each island then gets a compact mixed orchard
   on one town flank, a quarry on the other, and two wheat-field and two woodland
   target reaches on separate banks. Only the patch anchors are jittered; crops
   grow out as connected patches instead of independent random tile samples.
   All placements stay on their original island, outside town plots and sand
   approaches. The shared supply backstop uses that same outside-town mask and
   preserves the permanent deposits' initially clear gathering frontage.
7. **Check the finished world.** Require walking connectivity between colonies,
   unchanged designed grass/water terrain, no wheat or wood inside any reserved town,
   compact, complete island orchards and bounded quarries, and at least 64 reachable, free 4×4
   building origins on every island. These origins overlap; they do **not** mean
   64 independent buildings. Swarms, resources and units already occupy their
   final positions when room is measured.

### Fields, woods, quarries and orchards

Revision 5 replaces the old grid-patterned, per-tile random resource decisions.
It preserves the existing terrain, protected towns, seat assignment and routes.
The new placement treats each useful island as an opening economy and an expansion area:

- **Wheat fields** use fertile grass 2–14 steps from pure river water. The island's
  default ambient budget is one eighth of that useful bank area, clamped to
  96–768 tiles, divided between two target reaches.
- **Woods** use separate reaches of the same bank habitat. Their default budget is
  one sixteenth of useful bank area, clamped to 64–384 tiles. This gives renewable
  food more room than the faster-spreading timber. Fields and woods grow as compact
  patches around seeded anchors; water, roads and other deposits can split or limit
  a patch. Shortfalls and actual patch counts are reported rather than filling
  towns or raising river ground.
- **Quarries** are single connected outcrops near an inland town flank, with a
  default budget of 12 tiles per island. The stone amount scales that budget;
  connected space can limit it.
- **Orchards** are compact groups on the opposite flank, with cherries, oranges
  and prunes together. Each fruit has three connected trees at 100%, one or more
  at positive low amounts, and nine at 300%. Zero fruit removes orchards entirely.
  The complete orchard and initial gathering frontage must fit before planting;
  later fields, woods and resource repairs preserve that frontage. Fruit trees
  are never scattered individually across the island interior.
- **Algae** uses the shared small-clump seeding beside water, scaled by its amount.

Ambient budgets scale with their corresponding resource controls. The starter
food and timber remain independent of those controls. Orchard and quarry gathering
space is an opening reservation, not a no-growth zone: ordinary crops can expand
there later. Permanent town rims and ford approaches retain their existing
long-term protection. These allocations are intended to give ordinary farming,
mining and fruit gathering meaningful locations; they do not establish competitive
balance or guarantee an AI chooses a good expansion.

### Keeping the approaches open

A ford must have usable approaches as well as a dry crossing. Bank vegetation can
otherwise isolate towns from an intact crossing, especially for controllers that do
not clear their own routes. Both ends of every fitted ford connect to the protected
town rim on that island with a three-corner-wide sand approach. Island labels are computed
before fords are laid, so the nearest-clearing search cannot cross a river to
service the wrong bank. Shared Dijkstra routing uses cardinal/diagonal costs
10/14 and blocks water and town footprints. Widening never paints water or the
reserved town corners. These paths deliberately cost some farmland and open
several permanent military approaches; they do not change river crossings or
require workers to clear vegetation before an army can leave home.

The regression test fills the grass reachable by existing crops, conservatively
ignoring fertility, then verifies colony connectivity and usable island clearings.
Actual buildings and
combat can still change access later; this is a crop-containment guarantee, not
an unlimited traffic-capacity claim.

These are design heuristics, not established engine survival thresholds. Beaches
can carry units but not buildings. A protected clearing makes room for feeding,
training and swimming infrastructure; army throughput, early pressure and the
value of an outpost still need playtesting. Channels are narrow enough that towers
can threaten parts of an opposing bank: swimming and fords offer movement choices,
not immunity from cross-channel fire.

### Keeping resource repairs out of towns

Ambient resources can occupy bank kit sites and force emergency crop repair into
a protected town. Guaranteed bank kits are planted before ambient decoration, giving starter
supplies priority without taking town space or changing terrain. The validator also
rejects wheat or wood inside any town plot, even when today's free-building count
looks sufficient. Regression fixtures include the small seed and related 256×256
and 512×512 extremes. The [bulk audit](https://github.com/Globulation2/glob2/blob/evidence/braided-delta-generator/artifacts/braided-delta/pr-assets/README.md)
retains the failure, the corrected sweep, and the supported/invalid request split.

## Controls and supported envelope

| Control | Range; default | Effect |
| --- | --- | --- |
| Braid count | 2–5; 2 | Number of longitudinal channels and island lanes |
| Rejoining frequency | 1–3; 2 | Higher values shorten side-channel intervals |
| Island size | 32, 40, 48; 32 | Minimum land budget; larger values also lengthen islands |
| Crossing spacing | 32–96, step 16; 64 | Target spacing along main channels; side-channel fords remain |
| Resource amounts | Shared percentage ranges; 100% | Ambient wheat, wood, stone, algae and fruit; starter crops remain at zero |

Map dimensions must each be at least 128 tiles. Both rectangle orientations work;
maximum dimensions and worker counts follow the shared catalogue (512 and 8).
The exact accepted colony count is at most `braids * intervals`, subject to the
shared team limit. At defaults, 256×256 fits four islands and up to four colonies.
128×128 requires two braids; use rejoining frequency 3 for four colonies.
High braid counts on narrow maps are deliberately rejected. Interval counts are
whole numbers, so nearby settings can produce the same count on a small map.
The final footprint check can reject a seed if its curved shores invade a clearing;
seed/settings sweeps should record those rejections as well as generation failures.

Telemetry records effective island length, channel separation, island count,
orientation, requested/placed crossings, omitted-crossing events, adjusted clearings,
connected ford endpoints, approach sand-corner count, longest approach, starter supplies,
useful bank area, field/wood targets and actual patches, quarry targets/placement, and orchard locations.
Shared settlement/resource records explain placement and topups. Instrumentation
adds no RNG draws or terrain analysis when collection is disabled.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[BraidedDeltaGenerator.cpp](../../src/map/generator/generators/BraidedDeltaGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
