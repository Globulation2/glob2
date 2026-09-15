# Forts

**Forts** (`forts`, numeric ID 34) is a medieval, central-European-inspired landscape:
stone enclosures open onto wooded uplands, rocky crowns, winding river valleys,
irregular lakes, market-town sites, fields and orchards. It is an imagined countryside, not a geographic
reconstruction of Europe.

Each colony starts in a rectangular fort with corner bastions, two opposite gates,
a broad construction courtyard, separate irrigated wheat and wood plots, and a small
household orchard. Shallow moat sections supply water around each estate.
Sand borders contain those plots; sand roads through the gates resist crop growth
and connect the colonies by land, crossing rivers at fords. The opening supports
settlement inside the fort; larger economies and additional fruit collection encourage expansion
into the countryside. Swimming adds river crossings but does not bypass stone walls.

Ramparts are permanent stone **resource deposits**, not owned buildings. They also
provide quarry access and cannot be demolished. Towers are built by the players;
none are granted at generation. Sand gate roads cannot carry buildings, so defense
comes from the flanks rather than sealing the road with a building. This favors
fortified openings with two approaches rather than fully closed castles.

A broad Rhine-like river bends around the forts, with irregular lake basins and
tributaries. Roads favor valleys, reuse junctions and cross water at fords. Wooded
uplands and rocky crowns separate approaches. Market-town sites favor nearby ground on each fort’s side of the countryside,
with four protected building plots, crossroads and nearby fruit where space permits. They are reserved
before the river is routed, so water bends around them. Both gate destinations are
chosen together to limit total travel distance to distinct road hubs. These
are places for players to develop inns and outposts, not prebuilt neutral buildings.
The intended choice is whether to secure crossings and orchards early or consolidate
the fort first. Town sites may be omitted when a crowded map cannot fit them.

## Controls and limits

| Control | Default | Range and meaning |
| --- | --- | --- |
| Home size | 22 | 20–26, step 2; fort half-width in tiles, excluding projecting bastions |
| Gate width | 6 | 4–8, step 2; pure-sand core width, with walkable margins beside it |
| River width | 9 | 3–13, step 2; nominal water-corner width, locally narrowed between forts and interrupted by fords |
| Lakes | 1 | 0–3 requested basins per fort; each targets roughly map area / (16 × colonies) water corners |
| Village size | 10 | 8–12, step 2; half-width of the market-town sites |
| Resource amounts | 100% | 0–300%, step 25; wheat, wood, stone, algae and fruit |

Every home retains at least 32 wheat and 32 wood tiles at zero abundance. Surplus
planting scales with the crop sliders and caps at the contained plots' capacity.
Each household orchard has three tiles of each fruit at 100%, scaling to zero or
nine at the slider extremes. Structural fort stone remains at zero stone abundance; rocky uplands and scattered
quarries scale. Ambient resources stay off the reserved roads and home buffer.

Shared sizes are 64, 128, 256 and 512 tiles on either axis, with 1–12 colonies and
1–8 starting workers. The actual envelope depends on spacing: each pair of lattice
sites must differ by at least `2 * home-size + 16` in one wrapped coordinate, and
the shorter map side must also meet that minimum. Unsupported combinations are
rejected explicitly. Examples at defaults: 64×64 supports one colony, 128×128
supports four, and 256×256 supports twelve. Odd counts and rectangles are supported
when the same spacing test passes. Increasing fort size can invalidate a crowded map.

Sites are dealt randomly to colony indices. Homes share a construction and crop
budget; surrounding terrain and routes are asymmetric. This is not an exact-symmetry
or competitive-balance guarantee. The map wraps: forts can cross the preview edge.
The smallest maps devote much of their area to forts; 256×256 gives the countryside
more prominence.

## Implementation and validation

`FortsGenerator.cpp` reconstructs its layout for validation. It uses shared lattice
sites, periodic noise, sand/beach sketches, cheapest walking routes, settlements,
fertility fields, planting, wall isolation and building-footprint helpers. No shared
primitive or simulation behavior changes. Existing save/replay/network formats and
generator IDs remain unchanged.

Finished-world checks require every designed wall deposit, resource-free roads,
no entrance outside the two gateways (with gates shut), at least 64 clear 4×4
building anchors inside each fort, walking connectivity between colonies, and at least 16 clear 4×4 anchors per
placed market town.
These anchors overlap; they are not a count of independent buildings.

Telemetry records fort dimensions/counts, river orientation and fitted width, lake target/actual area and capacity limits,
town placement/omissions, plot target/actual
planting and summed growth potential, household orchard counts, plot-capacity limits, forest tiles and rocky-upland tiles, alongside shared
settlement and starting-resource observations. It makes no random draws or extra
map scans for collection.

## Reproduce and inspect

From the repository root:

```sh
scons release=1 server=0 -j4 map-generator-defaults-test map-generator-golden-test build/src/glob2
build/src/glob2 --list-map-generators forts
GLOB2_USER_DIR=/tmp/glob2-forts-preview build/src/glob2 --generate-map forts \
  --seed 1 --width 256 --height 256 --teams 4 \
  --output artifacts/forts/forts-1.map --preview artifacts/forts/forts-1.png \
  --json artifacts/forts/forts-1.json
build/src/MapGeneratorDefaultsTest glob2-forts-contracts
build/src/MapGeneratorGoldenTest glob2-forts-golden --require-rows
build/src/MapGeneratorGoldenTest glob2-forts-telemetry --telemetry
python3 tools/map_telemetry.py collect --generators forts --seed-start 20001 \
  --count 8 --set width=256 --set height=128 --set teams=3 \
  --jobs 2 --out artifacts/forts-new-study
```

The existing defaults harness includes Forts checks for the smallest fort with
maximum workers, minimum/maximum resource amounts, an invalid crowded request,
unharvested growth for 20,000 resource steps, and rejection of deliberately removed
ramparts and household orchards. CI already builds and runs this harness.

### Playtesting and evidence

See the [playtest report](https://github.com/Globulation2/glob2/blob/evidence/forts-generator/README.md)
for previews, exact requests, native maps, per-game results, compressed telemetry,
selected final saves, and before/after positional statistics. The report separates
the original tuning seeds (17, 29, 43, 61) from fresh seeds (101, 211, 307, 419).

Forts is an asymmetric landscape. Equal enclosed starter kits and connected roads
are structural guarantees; equal match outcomes are not. The initial tournament
exposed persistent weak starts, prompting larger irrigation ponds, moat sections,
household fruit, nearby town reservations and joint gate-road assignment. Read the
reported remaining position differences before treating this as a competitive map.

Automated games use Nicowar and a 60,000-tick cap. A cap leader is ranked by prestige,
then surviving units, then completed buildings; it is not an outright winner.
Rotating team labels controls label effects but does not make the four observations
of a map independent samples of terrain. Human enjoyment and defensive pacing still
need playtesting.

Revision 6 explicitly sequences lattice RNG draws across compilers. All 32 native
map files used in the final Nicowar study are byte-identical to revision 5, so this
portability fix preserves the tested layouts.

The final parameter matrix passed 60 attempts, covering rectangles, up to twelve
colonies, resource extremes and layout extremes. The defaults harness checks 20,000
unharvested resource-growth steps. Platform-specific golden fingerprints are recorded
from actual macOS arm64 and Linux x86_64 executions; no existing generator rows change.
A separate 4,096-tick revision-3 probe matched detailed per-tick simulation checksums
across those platforms, although its save/replay containers were not byte-identical.
This is not a full-match cross-platform or save/load-continuity claim. Existing
simulation code and serialization formats are unchanged by Forts.
