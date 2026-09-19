# The Comb

The Comb (`comb`, numeric ID 62, revision 2) creates two curving shores with broad,
interlocking peninsulas. Land around both ends of the inlet connects the shores;
outer sea keeps the toroidal seam from creating another walking route. Opponents
can threaten a nearby waterfront while their armies take a longer land route.
Swimming opens direct cross-channel attacks and outer-coast approaches.

## Landscape and economy

The coastline follows irregular bends and offset peninsula noses, with two larger
asymmetric bays. Narrow facing sections offer firing positions for towers after
one upgrade (range 7). Towers prioritise units but can also target buildings.
Colonies receive the standard swarm and workers; no military buildings, schools
or trained military units are granted.

Mainland farming ribbons follow the outer coast. Sand caps, cross aisles, and
separate wheat/wood compartments contain their future spread. The mainland has
stone and mixed fruit. Forward peninsulas retain building ground and sand supply
lanes; their primary reward is military position rather than exclusive resources. Paths stop short of the noses to leave
whole forward footprints clear.
Normal growth rules apply everywhere. Each wheat plot starts with a small grass
clearing beside compact guaranteed grain, so an inn can establish a feeding edge.
Those farm clearings can regrow; the sand-protected mainland and forward building
ground are the permanent construction reserve.

Irregular sandy clearings and occasional wheat/wood pockets break up the interior.
Their sizes and outlines come from smooth noise, without a repeated placement grid.
Each crop pocket has a closed sand rim; natural growth can fill its grass centre but
cannot escape onto nearby construction ground. Ambient crops follow their existing
amount sliders and disappear at zero. Farms, starting ground, supply roads and the
central part of each forward position are excluded from this decorative layer.

The long dimension determines coastline orientation; square maps choose either
orientation from the seed. The inhabited landmass has a bounded transverse span
so larger square maps add outer sea instead of unbounded food hauls. Increasing
length provides more mainland and farmland. Colonies are selected on existing
land, split as evenly as possible between shores, compared using the finished
start scorer, and randomly dealt to team numbers. This is measured opening
quality, not symmetry or an assurance of equal expansion opportunities. Odd
colony counts necessarily put more colonies on one shore. Two-player maps place
both colonies near one randomly selected end, avoiding a half-map opening march
on the longer sizes.

## Controls and support

- Each side is 256 or 512 tiles. Both rectangle orientations are supported.
- Two to four colonies; up to eight when both sides are 512.
- One to eight starting workers per colony, default four.
- **Peninsulas per shore:** 2–4, default 3. More peninsulas create more fronts and
  divide the inlet into narrower landforms.
- **Wheat, wood, stone, algae, fruit:** 0–300%, steps of 25, default 100%.
  Crop compartments retain a starting floor (24 wheat or 12 wood tiles per
  compartment, limited by legal plot capacity); stone retains a mainland floor.
  Amount controls scale additional deposits inside their reserved ground.
  Fruit and algae have no floor. Wheat above 100% interpolates from the default
  density to full planting capacity at 300%, keeping the last slider steps useful.
  The initial inn clearings stay unplanted even at 300%.
- Default: 256×256, four colonies, three peninsulas per shore.

## Validation and reproduction

The generator checks its original terrain, both independent end connections,
absence of extra walking crossings, crop containment, actual gathering distances,
home building room, and disjoint accessible construction footprints on each
peninsula. Forward checks concern legal room and access; they do not establish
supply throughput or predict AI tactics. The engine's swimming passability must
be used when checking landing access, because algae can obstruct swimmers.

```sh
scons release=1 server=0 -j6 build/src/glob2 comb-generator-test
build/src/glob2 --generate-map comb --seed 7 --width 256 --height 256 --teams 4 \
  --output artifacts/comb/example.map --preview artifacts/comb/example.png \
  --json artifacts/comb/example.json
build/src/CombGeneratorTest comb-tests "$PWD" artifacts/comb/mechanism
```

`CombGeneratorTest` verifies complete-save repeatability with telemetry on/off,
terrain/resource save-load preservation, unsupported requests, missing food, and
cross-channel firing on generated terrain. Its controlled ammunition probe places
stone in a test tower; a separate empty-tower probe requires workers to harvest
native stone and deliver it before the tower can fire again. These controlled
mechanism tests are separate from normal AI development. Initial-map supply
checks do not claim sustained throughput after both proposed buildings are built.

## Revision 2: scattered ground

The decorative revision passed 1,232 additional requests: all 192 shape/team/peninsula
cases, all 272 paired slider cases, 128 resource-extreme combinations, 512 random
settings and 128 starting-worker cases. Each resource slider remained strictly
increasing across all four paired seeds. The full crop-envelope check includes the
new pockets; a regression deliberately planting grain on uncontained construction
ground is rejected.

Two 20,000-tick games on seed 401 (Cortex and Cabino) established 8–21 buildings per
colony, with no worker starvation. The reviewer checked the updated visual density
and sand-rim containment. The 512-square/eight-colony benchmark measured 464 ms/map
of process CPU time across 24 seeds, including the new decorative terrain work.
See the updated [preview](../artifacts/comb/preview.png) and
[scatter results](../artifacts/comb/scatter-validation.json).

## Revision 1 baseline results

The [evidence bundle](../artifacts/comb/README.md) contains previews, aggregate
statistics, test output, gameplay summaries and reproduction commands. Full raw
requests, reports, frozen binaries and saves are in `artifacts/comb/`.

- **4,256 successful maps:** 192 shape/team/peninsula cases, 272 slider cases,
  1,152 resource-extreme combinations, 2,000 random settings, 128 starting-worker
  cases, and 512 random settings also varying workers from one through eight.
- Every resource slider increased its resource count at every step on all four
  paired seeds. The peninsula control produced exactly four, six or eight total
  peninsulas. These are range-covering samples, not exhaustive combinations.
- Initial worker-to-grain distances were 3–13 steps and wood 3–14 in the main
  matrix. Each peninsula had two separate accessible 4×4 forward footprints.
  Initial access to the first proposed court was 71–145 steps for food and
  33–113 for stone. Firing-site counts are alternative placements, not simultaneous
  towers; ordinary AI games do not necessarily exploit every front.
- Review proceeded through successive geometry, visual, playability and profiling
  rounds. Both translation keys were independently reviewed in all 33 catalogs;
  strict catalog and font checks passed. Native-speaker review was not performed.
- Profiling removed repeated shore-footprint construction and per-colony resource
  scans. Across two 24-map batches at 512×512/eight colonies, mean process CPU
  time fell from about 439 to 407 ms/map (7%). Shared-machine wall times are not
  directly comparable. All 256 compared reports/telemetry and six complete saves
  remained identical; the mechanism and feeding-court tests also passed afterward.

## Baseline gameplay observations and limits

The revision 1 terrain was played in ten headless games (230,000 ticks total):
all four newer AIs on held-out seed 401, low/high wheat, three colonies, two colonies
on a long map, eight colonies on a large map, and a 40,000-tick Cortex game. Earlier
opening revisions also had eleven 20,000-tick games, including all four rotations
of seed 29. These are AI playtests, not human assessment of fun or a win-rate study.

Every final-game colony established additional buildings. Combat occurred in the
normal-density games; Cabino also fired towers. Low-wheat Cortex games grew more
slowly and remained peaceful through 20,000 ticks. The long Cortex game exposed a
late food-cap limitation: all colonies survived and expanded, but large armies
outgrew feeding throughput and suffered substantial starvation. Initial food
access therefore does not imply indefinitely sustainable AI population growth.
Players need to expand their mainland feeding economy as well as their army.
The outer coast's farmland and protected forward space remained legible in played
save previews. Odd-team expansion remains intentionally asymmetric.

The dedicated Comb tests pass, and all 456 available macOS golden rows match.
The broader defaults suite stops at the unrelated, concurrently developed Wadi
assertion; the golden coverage check also reports Wadi's missing rows. Those
files were left to their existing work. Cross-platform generation equivalence and
human playtesting remain unverified. No simulation rules, save formats, replay
compatibility gates or existing generator definitions are changed.
