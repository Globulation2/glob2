# The Comb

The Comb (`comb`, numeric ID 62) creates two curving shores with broad,
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

Mainland farming ribbons follow the outer coast, retaining broad uninterrupted
banks against the sea. Higher initial wheat coverage supplies a stronger opening
within those existing fields. Sand caps, cross aisles, and
separate wheat/wood compartments contain their future spread. The mainland has
stone and mixed fruit. Forward peninsulas retain building ground and sand supply
lanes; their primary reward is military position rather than exclusive resources. Paths stop short of the noses to leave
whole forward footprints clear.
Normal growth rules apply everywhere. Each wheat plot starts with a small grass
clearing beside compact guaranteed grain, so an inn can establish a feeding edge.
Those farm clearings can regrow; the sand-protected mainland and forward building
ground are the permanent construction reserve.

Broader irregular sandy clearings and occasional wood pockets break up the interior.
Small separated wheat tufts line the central wave-shaped inlet, directly beside
water. They add no sand enclosure, leave gaps for gathering and forward buildings,
and scale with wheat abundance. Their future spread follows fertile shores; the
mainland construction belt and both end routes remain protected. Short dry paths
across the inlet mouths stop crops spreading around the outer sea coast.

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
  Crop compartments retain a starting floor (48 wheat or 12 wood tiles per
  compartment, limited by legal plot capacity); stone retains a mainland floor.
  Amount controls scale additional deposits inside their reserved ground.
  Fruit and algae have no floor. Wheat above 100% interpolates from the default
  density to full planting capacity at 300%, keeping the last slider steps useful.
  The initial inn clearings stay unplanted even at 300%.
- Default: 256×256, four colonies, three peninsulas per shore.

## Playtest limitations

Interlocking shores make cross-channel positions and contained farms central to play. Compare actual supply and approaches for both roles; aggregate resource symmetry is not a balance measurement.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[CombGenerator.cpp](../../src/map/generator/generators/CombGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
