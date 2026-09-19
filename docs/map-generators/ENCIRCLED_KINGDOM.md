# Encircled Kingdom

A deliberately asymmetric siege landscape for **3–12 colonies**. Colony zero starts
inside a large fortified agricultural heartland. The other colonies occupy smaller
fortified towns outside its perimeter. More opponents make the siege harder; the
map does not compensate with extra starting units or finished economic buildings.

Use the existing **You vs all** team preset with the human in colony zero for the
intended siege, or set alliances manually. The generator does not set alliances,
change victory conditions, alter AI, or change any simulation rules. In a free-for-all
the same terrain works, but the outside colonies can fight each other.

## Landscape and choices

The kingdom has broad land gates, a moat, and two unwalled waterfronts that become
alternate approaches for swimmers. Ramparts are permanent stone **resource deposits**,
not destructible buildings. They also supply mining frontage. Control of ground and
forward buildings, rather than destruction of terrain, opens a front.

The capital has two renewable starter wheat gardens, a wood garden, and a nearby
quarry. Outer towns have one starter wheat garden, a wood garden and stone ramparts.
All colonies start with one ordinary swarm and the shared starting-worker count.
Additional irrigated fields inside the kingdom make its economic potential larger
than that of any single outer town. Losing a frontier can expose those farms and
provide an attacker with room for inns and forward construction.

Streets and visible sand boundaries contain crop growth. Dry countryside woodland
cannot regrow after being cleared. No tile disables the engine's resource-growth
flag. The exterior road, short lanes to nearby fronts, and town streets preserve circulation through the siege.

Three fortress plans alter the outline and internal streets: elongated enclosure,
bastioned enclosure, and paired courtyards. A seed also chooses one of three town
layouts, outside locations, gate positions, and distributed farm districts. Only
outside starts are shuffled; **colony zero always keeps the capital**.

| Colonies | Initial land fronts | Minimum map |
| --- | --- | --- |
| 3–5 | 3 | 256×256 |
| 6 | 4 | 256×256 |
| 7–8 | 4 | 256×512 or 512×256 |
| 9–11 | 5 | 256×512 or 512×256 |
| 12 | 6 | 256×512 or 512×256 |

512×512 works for every supported count. Both dimensions must be at least 256,
and aspect ratio must not exceed 2:1. The generator rejects unsupported requests
rather than silently removing colonies or enlarging the map.

Several attackers can share a front. A minimum-cost assignment using finished-map
walking distances balances the number assigned to each front within one, with
no assigned approach longer than 240 walking steps. Town placement considers nearby
fronts before streets and woodland are drawn. This is a
generation diagnostic and design guarantee; it does not issue orders to players or
teach AIs a special siege strategy.

## Controls

- **Fortress plan:** automatic, elongated enclosure, bastioned enclosure, paired courtyards.
- **Gate width:** 6–14, in steps of two, default 10; broadens the land crossings.
- **Heartland farmland:** 75–150, default 100; a size setting, not a literal percentage. Values
  75, 100, 125 and 150 give plot lengths of 18, 20, 22 and 24 terrain corners.
  Longer plots stay within reserved districts, preserving streets, courts and starter gardens.
- **Resource amounts:** standard 0–300 controls for wheat, wood, stone, algae and fruit.
  These scale surplus planting, dry woodland, outcrops and orchards.

Guaranteed supplies remain at zero abundance: 64 wheat tiles and 32 wood tiles in
each applicable starter garden (including at least 16 timber tiles within a
20-tile radius of the starting swarm), four wheat or two wood seeds in each expansion
plot, the capital's small quarry, and structural ramparts. Expansion seed guarantees
are necessary because sand containment prevents empty plots from being colonized
by a distant crop. Structural stone is not governed by the ambient stone control.
Resource density never changes the fortress drawing or the town positions.

## Implementation and validation

The generator owns its request check, reproducible design, terrain construction,
settlement, resource placement and final validator. It reuses the shared drawing,
morphology, garden planting, room, growth, settlement and movement tools. Existing
generators, save formats, network versions and replay acceptance are unchanged.

Checks cover complete town footprints across the torus, colony-zero placement,
accessible starter wheat/wood/stone, locally visible timber, building room, connected approaches, a closed
land perimeter when gates are blocked, independently usable swimming waterfronts,
resource containment, and a larger allocated food/building potential for the
capital. Food and room ratios describe opportunities, not guaranteed game outcomes.
The existing lobby equality score is unchanged; every accepted candidate must
retain the capital's intended advantage regardless of which roll the lobby picks.

Telemetry keys under `kingdom.*` describe plan, town layout, front count and balanced
assignments, walking distances, farm capacity and planting, dry scenery, growth
potential, connected building sites and capital-to-outer-town ratios. Nearby food
potential includes crop ground made accessible by harvesting; attack distances use
actual initial walkability. Collection
must not change any generated tile or random draw.

Generate a reproducible preview and playable map:

```sh
build/src/glob2 --generate-map encircled-kingdom --seed 7 \
  --teams 4 --width 256 --height 256 --set fortress-plan=3 \
  --output artifacts/encircled-kingdom/example.map \
  --preview artifacts/encircled-kingdom/example.png \
  --json artifacts/encircled-kingdom/example.json
```

Evidence and measured limitations are recorded in the accompanying validation
[report](../artifacts/encircled-kingdom/README.md). Generation repeatability is a per-platform contract; no cross-platform
simulation checksum comparison is claimed for this map-only addition.
