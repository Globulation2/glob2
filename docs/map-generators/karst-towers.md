# Karst towers

**Karst towers** (`karst-towers`, numeric ID 54) is river country among limestone
pinnacles, the way Guilin or Ha Long Bay look. Thickets of limestone towers stand on open grass with
sinkhole ponds between them, rivers wind along the valleys between the rows of homes, and the flat
ground on their banks is terraced into paddies: strips of wheat between flooded strips. Every
colony's home is a bowl in the karst, a clearing walled by a ring of towers with gates, and a doline
lake in a ring of fields lies between neighbouring homes.

## How it plays

- **Gates are the only ways home.** The towers are permanent terrain walls (revision 4; earlier
  revisions stood them in stone on grass), so a bowl's ring cannot be mined and its gates (one,
  two, three or four, by the map's design) are where a colony is attacked. The ring is travertine
  (Obstacles, which stop arrows), except for low outcrops (Ridges) flanking each gate, which a
  bowl's towers can shoot over. Every bowl has the same two quarry faces at the foot of its ring,
  as far from its gates as they can be (18 stone at the defaults), and one irrigated paddy's worth
  of food.
- **The terraces are the surplus, and they are the front.** Rivers run between the rows of homes,
  so the river terraces lie between rivals. Fords, evenly spaced along each river, are where the
  rows meet; swimming turns a river from a wall into a shortcut through the paddies.
- **Food follows water.** Crops regrow by the density of pure water near them, so every field the
  map plants has water beside it: the home paddy's channel, the flooded terraces, the doline lakes.
  Flooded terraces trade wheat area for regrowth.

## How it is built

`src/map/generator/generators/KarstTowersGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made. The design is a sequence of stages:

| Stage | What happens |
| --- | --- |
| Homes and rivers | Homes on a lattice (`latticeSites`), dealt at random. Rows of homes are found across one axis, and a river goes in the middle of every gap between rows wide enough for two of the smallest bowls and the river. On a square map the axis with more such gaps wins; of an even number of four or more rivers, every other one is kept. With no gap that wide, one river runs free round the bowls. Homes are sized to leave their rings and the rivers room. |
| Towers | The spots of a Turing pattern (`turingPattern`). A slow fractal noise lowers the cut where the karst is thick, so towers grow fatter and closer in thickets but never pool into one mass. |
| Bowls | A clearing, a solid ring three tiles deep with a lumpy outer edge, gates, and an apron. Ring edge, lumps and gate pools are read from one noise stencil at each tile's offset from its home, with one gate design and one facing per map, so every bowl is drawn the same. Gates face across the rivers. |
| Rivers | The cheapest walk round the torus through waypoints inside each river's band (`cheapestWalk`), dear within eight tiles of a tower, with a noise term for meander. A banded river may only step forwards along its axis, so it cannot double back. |
| Fords | Boardwalk bridges straight across each river, every corner of a crossed tile paved, evenly spaced along it from half a spacing past the first home, so with as many fords as homes in a row they fall between the homes. A quarry face (8 stone at 100%) stands at the towers nearest each landing. |
| Lakes | One lobed doline lake halfway between neighbouring homes in a row, the same shape each time, left out where it would touch a bowl or come within eight tiles of a river. |
| Sinkholes | Ponds at troughs of the tower field beyond the valleys, and from each a chain of pools towards the river that stops before the paddies, with land between the pools. |
| Valleys | Towers thin out towards each river, and none stands on a shore or a ford's landings. |
| Terraces | Bank bands by distance from the river (9 tiles of crops, 6 of water, repeating), cut across every 24 tiles of river by a flood of the centreline's index. A field's corners are bunds where the next field starts, one row of earth-trail corners (Paths) each: nothing grows across them, and unlike sand they starve no crop beside them. Dry paddies, lake fields and home paddies are spring meadow (Fertile). Water-ribbon paddies are flooded on every corner inside their bunds. A few crop-ribbon paddies are flooded too. Lake fields are six sectors in a ring 2–10 tiles round each lake, cleared of towers and never flooded. |
| Home water | A bunded home paddy north of the middle with a two-tile channel, a doline pond on the clearing's rim (the starter kit is planted round it), and a lobed pool beside the first gate and the third, where the design has one. |

The map has one fixed look of its own (`kKarstLook`, not a shared climate): pale travertine
pillars, outcrops at the gates, earth-trail bunds, boardwalk fords, spring-meadow terraces, scree
talus frayed round the towers out in the karst (never in a bowl, a paddy or on a shore), and a deep
channel down any river 9 or more wide, three corners in from its banks. Shallow banks keep the
algae and the irrigation. The flooded terraces stay water: as spring meadow they would water their
neighbours at under a fifth of water's strength.

Resources follow: the home paddies sown whole, a share of the dry terraces sown whole, the lake
fields in the same patches round every lake, woods in the thickets and in broken patches at tower
feet (wood that the water could spread is kept 16 tiles from bowls and lakes), orchards on open
ground off the bowls and paddies, and algae in the shallows.

A crowded map whose big homes or wide rivers leave a river no way through is not refused at once:
the design shrinks the homes two tiles at a time to the smallest home size, then narrows the rivers
to the narrowest, and telemetry records each step.

## Controls

| Control | Range (default) | Effect (6 seeds, 256×256, 4 colonies: low / default / high) |
| --- | --- | --- |
| Tower spacing | 14–32 (16) | The towers' wavelength; mostly visual. Wider spacing finds fewer sinkhole troughs (4.2 / 3.0 / 0.8 per map). |
| Tower density | 0–100 (50) | Thicket extent and lone towers: stone 4.9% / 10.2% / 13.8% of the map. |
| River width | 4–16 (7) | Water 9.0% / 12.3% / 20.6%. Crowded maps narrow it. |
| Fords | 1–6 (2) | Fords per river. |
| Paddy depth | 16–40 (24) | How far the terraces reach from a river: water 11.8% / 12.3% / 15.6%, buildable land 40.6% / 35.9% / 28.5%. Below 16 no flooded strip fits. |
| River meander | 0–100 (50) | How much the rivers wind; at 0 they keep to the middle of their bands and bend only round towers. Visual. |
| Flooded terraces | 0–100% (88) | Share of the water strips flooded, and in proportion a few crop strips: water 7.8% / 12.3% / 13.2%, wheat 10.9% / 7.9% / 7.4%. |
| Sinkholes | 0–300% (100) | Number and (up to double) size of sinkholes: water 12.1% / 12.3% / 15.9%, sinkholes 0 / 3 / 33. |
| Lakes | on/off (on) | Doline lakes and their fields: water 10.9% / 12.3%, wheat 5.9% / 7.9%. |
| Home design | Random, Horseshoe, Twin gates, Three gates, Four gates | Gate layout; the shortest rival walk is 124–147 steps by design. |
| Home size | 12–22 (15) | Clearing radius; crowded maps shrink it. |
| Wheat amount | 0–200% (100) | Sown share of the dry terraces and lake fields (terraces are fully sown by 200). Home paddies and the starter kit are guaranteed. |
| Wood, algae, fruit amount | 0–300% (100) | Linear. Towers are structural and not scaled by any amount. |
| Stone amount | 0–300% (100) | The ford quarry faces. The bowls' quarry faces are guaranteed. |

## Limits

- On crowded maps (256×256 with 8 colonies) the rivers' bands shrink to nothing and the rivers run as
  straight canals; on very narrow maps (512×128) rivers and terraces flatten into parallel stripes.
- Bunds and beaches make much of the map mixed ground (buildable land about 36% against Forts'
  52% at revision 3; not remeasured for revision 4).
- Sinkholes near 300% give about 30 ponds on a 256×256 map and look blobby.
- Two colonies on a non-square map can score low on the start-fairness model (0.51 at worst in the
  random rolls).
- No rotation tournament or human playtest has been recorded.

## Playtest limitations

Home crop containment and approach geometry need checks after growth; nearby fertile terrain alone does not establish that an AI will farm it.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
sim-diff map fingerprints, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[KarstTowersGenerator.cpp](../../src/map/generator/generators/KarstTowersGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
