# Drowned Forest

Drowned Forest combines wet woodland islands, permanent sand routes and meadow
settlements found in the landscape. Players can follow the coastal route or put
workers into clearing a timber neck for a shorter trip to another usable meadow.
Normal wood growth can reclaim the cut. Sand routes and construction meadows stay
open through growth, so maintaining a shortcut is a choice rather than a survival
requirement. Shared shore meadows and elongated sandbar shoals offer forward bases;
swimming provides later alternatives.

The generator composes relaxed scattered sites, warped territories, lobed shores,
a near-tree crossing graph, wandering paths, contained farmland and scored start
selection. It uses ordinary terrain and resources, grants only the normal starting
swarm and workers, and never disables resource growth. No simulation or save-format
change is required.

## Controls and supported requests

| Control | Intended effect |
| --- | --- |
| Sandbar connections, 0–100 | Adds links beyond the connected network and required second island connections. More links offer more approaches. |
| Wooded neck thickness, 3–9 | Changes the depth of each five-tile-wide timber plug: 15, 25, 35 or 45 wood tiles. |
| Neutral clearing size, 12–24 | Enlarges forward meadows and junction shoals. Colony construction room is a separate guarantee. |
| Wheat amount | Increases seeded grain beyond the home/forward-base minimum, up to physical plot capacity. |
| Wood amount | Increases ambient forest coverage and planted timber. Structural timber shoulders remain at zero. |
| Stone amount | Increases deposits outside reserved building footprints and gathering faces; two starter deposits per home remain at zero. Fractional rounding distributes intermediate steps among meadows. |
| Algae amount | Increases initial water patches; each colony retains three reachable algae tiles at zero. |
| Fruit amount | Increases orchard seeds around neutral meadow edges. |

Resource controls cover 0–300%. Ambient forest probability is `amount / (amount +
25)`, so high wood settings continue changing coverage. This describes initial
coverage; subsequent normal growth changes it. Plot capacity can limit abundance,
and the finished-map search can select another landscape when a control changes
which starts meet the contract. Connection counts are discrete: small island graphs
may have no unused edge to add at an intermediate setting. Abundance controls
change initial resource coverage, not the crop growth rules.

Supported maps have square or 2:1 proportions and sides of 128–512 tiles:
128×128 supports one or two colonies, 128×256 and its transpose support up to four,
and maps with both sides at least 256 support up to eight. Workers cover the normal
one-to-eight range. Other requests are refused explicitly.

## Construction and validation

Colony sites must fit a meadow and separate grain and timber plots on existing
islands. Farm orientation follows the available ground. An adjoining pool supports
regrowth, and swarm placement faces the actual grain patch to shorten the opening
food haul. One row of sand corners contains each plot without thick paved rings.
The shared farmland helper retains its original two-row default for other callers.

Woodland islands carry irregular timber shoulders reaching their natural coasts.
Two sandy inlets almost meet across each designated plug; the coastal detour stays
open. This structural wood is required even at zero ambient forest. Starts and
forward meadows are chosen and connected using actual toroidal walking distances.
The search is deterministic and bounded; it never relaxes the validation contract
to accept a difficult request. On 128×128 maps it tries alternate sides for forward
meadows before discarding the landscape, with at most 512 landscape attempts;
Fully occupied 128×256, 256×128 and 256×256 maps use at most 256, and other requests use at most
64. Difficult compact or crowded requests can therefore take longer.

The finished-world checks require:

- Connected colonies and meadows on routes that remain open after growth.
- Six independent 4×4 home building footprints with circulation, two independent
  three-wide meadow exits, and renewable home crop and reachable algae supplies.
- A shared, food-bearing forward meadow with at least three building footprints
  within 70 walking steps, with bounded arrival differences between rival colonies.
- Contained crop components, with no growth connection into construction meadows.
- Closed timber plugs whose removal saves at least eight steps and 25% of the
  mouth-to-mouth walk. Every colony must also benefit on an actual trip to another
  usable meadow. Shortcut measurements use current resource-aware walking, rather
  than pretending that empty forest grass is already blocked.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[DrownedForestGenerator.cpp](../../src/map/generator/generators/DrownedForestGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
