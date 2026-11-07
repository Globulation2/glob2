# Caravanserai

**Caravanserai** (`caravanserai`, numeric ID 41) is oasis towns on a desert trade route. Dunes run
in long bands, wind-rippled on their crests and pale in the hollows, with dry scrub between them;
flat-topped mesas stand out of the sand on skirts of scree, their stone quarried from faces cut
into their flanks; scattered oases are irregular pools ringed with spring meadow, lush grass and
date palms, drying out towards the sand. Every colony's home is a large oasis town round its own
lake with a ring of fields and a palm grove. Half way between neighbours stands a caravanserai, a
square walled courtyard, paved, with a well, in an oasis of its own with orchards and a quarry, and
the caravan routes between them are roads.

Revision 5 (Caravanserai 2.0) rebuilt the map for the engine's full terrain palette; revision 4
drew everything from grass, sand and water, its mesas were grass covered in stone and its routes
were lines of sand.

## How it plays

- **Sand can be crossed, never settled.** Sand holds no building and no crop, so every forward inn,
  tower or barracks stands on an oasis or beside a road, and the oases are what a colony expands
  along.
- **The roads are the logistics.** Each colony's road to each caravanserai it shares is its own
  cheapest walk from its town to its own gate, and runs on through the gate into the paved
  courtyard. Road ground is path: twice walking speed, buildable, and nothing grows on it, so
  carriers and armies move along the trade route far faster than across the dunes, and an inn or a
  tower on the road holds it.
- **Mesas are rock; stone is a place.** Mesa rock is impassable and permanent and towers shoot over
  it (the Ridges rule group). Stone stands only on quarry faces cut into the mesas' flanks, chosen
  nearest the homes first and spaced apart (`stone-amount` sets how many), on the caravanserais'
  quarries, and in the caravanserai walls, which are stone deposits and man-made walls at once.
  Scree skirts the rock: slow, unbuildable, nothing grows.
- **Canyon country is slow.** Its mesas stand on hardpan floors, walkable at reduced speed, with no
  buildings or crops; only the roads cross it quickly. The floors keep 18 corners off every home,
  so no colony's dooryard is slower than another's.
- **Oasis shores feed.** Spring meadow (crop land that waters its surroundings) rings every neutral
  oasis pond and the caravanserais' ponds, inside the lush grass. The homes are unchanged: lake,
  field ring and palm grove on plain grass, the kit guaranteed.

## How it is built

`src/map/generator/generators/CaravanseraiGenerator.cpp`; the header comment and the comments on
the constants record why each choice was made. The design keeps a mechanic sketch (the canonical
type of each rule group: grass, sand, water, path, rock, scree, hardpan, spring meadow) that every
shared helper and check reads, and paints looks over it with `composeLooks` (`shared/Sketch.h`),
so looks never change a rule.

| Stage | What happens |
| --- | --- |
| Homes | One stencil stamped at every lattice site, turned by one quarter turn drawn per map: an outline of `oasis-size` radius, a lake at the back, a six-tile field ring closed off by a sand path, a palm grove between two sand spokes, the swarm on the widest town tile. Homes shrink by two down to 16 to make room for caravanserais, then caravanserais are left out. |
| Caravanserais | One at the midpoint of every pair among each colony's `caravanserais` nearest neighbours, wherever its oasis of radius 14 fits: a wall at Chebyshev 6 with three-tile gates at both ends of the route, the courtyard paved on every corner no wall tile touches, a well in the middle, a pond to one side. |
| Oases | Route oases every 32 tiles along each colony's straight way to each caravanserai it shares, then scattered oases by `oases` (one per 3,200, 1,800 or 1,100 desert tiles). |
| Roads | Every colony's cheapest walk to the gate facing it, round the features and never through a courtyard. The walk's cost adds broad swells of noise 40 tiles across, up to 24 on a desert step that otherwise costs 10 to 18 (oases cost nothing extra), from its own stream (`caravanserai-going`), so a road bends round heavy going like a track across real sand instead of running ruler-straight between two homes on the lattice; the mesas and dunes draw as before, kept off the new roads. A road tile takes path on all four corners except corners any home, oasis or wall tile owns, so the homes stay identical; the road crosses the caravanserai's oasis and runs through its gate. |
| Mesas and quarries | Mesas are the top 0, 5 or 11 percent of a relief noise by `desert`, six tiles clear of everything a colony needs. Rock vertices only where all four tiles are mesa (`rockVertices`); quarry faces are mesa edge tiles left out of the rock, one per 32 tiles of mesa edge at 100% stone, at least 12 apart (6 at 200% and above), where the mesa is at least three thick, nearest the homes first. The quarry tiles and the ring before them stay plain grass. |
| Talus and floors | Scree one corner out from the rock, two where a fray noise passes; in canyon country, hardpan on the next 12 percent of the relief, fading out beyond 3 corners from a mesa and within 18 of a home. |
| Dunes and scrub | Stripes wrapping the torus about 12 tiles apart; single grass corners of scrub in 16% of the hollows and 30% of a three-tile halo round every oasis, never two side by side. |
| Shores | Spring meadow on the corners two and three steps from every neutral or caravanserai pond. |
| Looks | Dune crests wind-rippled, hollows pale; scrub and oasis rims dry, shores lush; the country's rock, rims, talus, road and courtyard artwork; a dry rim round every town, the same on every home. |
| Resources | Wheat in a solid arc of the field ring nearest the swarm (at least 60), palms in the grove (at least 30), caravanserai groves, quarry, grain and palms, oasis palms and grain, algae, quarry-face stone. |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Home oasis size | 18–30 (24) | The home's radius; shrinks to fit crowded maps. |
| Oases | Sparse, Normal, Many (Normal) | Scattered oases per desert tile. |
| Caravanserais per colony | 1–3 (2) | Caravanserais to each colony's nearest neighbours, each with its roads. |
| Desert | Open erg, Dunes and mesas, Canyon country, Random (Random) | The country. Open erg: dunes, no rock, earth trails, a sandstone court. Dunes and mesas: 5% of the map in sandstone hoodoo mesas on scree, flagstone roads, a brick court. Canyon country: 11% in grey rock mesas rimmed with hoodoos, on hardpan floors, earth trails. |
| Wheat, wood, algae, fruit amount | 0–300% (100) | Linear; the home's kit floors are guaranteed. |
| Stone amount | 0–300% (100) | Quarry faces on the mesas and the caravanserais' quarries; the caravanserai walls are always stone. |

## Food

Growth potential (the engine's crop growth chance summed over farmland, rules-aware:
`growth_potential.c`), 256×256, 4 colonies, seeds 1–3, revision 5 against revision 4 (master release
build), each country pinned:

| Desert | Total (r4 → r5) | Within 24 of swarm, mean (worst) | Within 48, mean (worst) |
| --- | --- | --- | --- |
| Random (default) | 142 → 153 | 22.9 (21) → 25.7 (24) | 28.8 (27) → 33.2 (32) |
| Open erg | 142 → 153 | 23.0 (21) → 25.7 (24) | 28.8 (27) → 33.2 (32) |
| Dunes and mesas | 145 → 153 | 23.0 (21) → 25.7 (24) | 29.1 (27) → 33.3 (32) |
| Canyon country | 149 → 156 | 23.3 (21) → 25.7 (24) | 29.6 (28) → 33.3 (32) |

Food near every home rose 12% within 24 steps and 15% within 48: road and scree corners replace
sand corners, and only sand inhibits growth. The total rose 5–8% with the meadow shores. The stamped
homes are unchanged; wheat planted in each home is the same 87 tiles at the defaults.

Stone: walking steps from each swarm to the nearest stone, the same seeds (12 colonies a country):
Dunes and mesas 31.6 mean (worst 45) in revision 4, 33.2 (49) in revision 5; Canyon country 22.2
(35) and 23.8 (36). Open erg has no mesas in either revision. Each colony has a third to a half as
many stone tiles within 48 steps as before, where every mesa tile was a deposit.

## Limits

- Home oases are identical copies on a lattice: fair, and visibly stamped. What lies round each home
  (which mesas, oases and floors) is noise that the lobby's best-of-five start scoring evens out.
- Roads are each colony's own cheapest walk, so their lengths differ where mesas and oases lie
  differently; the validator bounds the walk to the nearest caravanserai gate (32 steps) but not
  the share of it on road.
- Stone is scarcer than in revision 4, where every mesa tile was a deposit; open erg's only stone is
  at the caravanserais.
- Canyon country's floors and scree slow the ground between oases; armies that leave the roads pay
  for it.
- No rotation tournament or human playtest has been recorded for revision 5; one four-AI
  calibration game per revision is the only game evidence.

## Verification

Run the Caravanserai contracts and the shared Glacis/Allotments/Caravanserai block
(`GLOB2_CONTRACTS=caravanserai,rebuilt-landscapes`, `MapGeneratorContracts.h`) and the platform
golden rows, inspect small and large rectangles and resource extremes, then check populated games
with complete seat rotations. The validator proves every caravanserai wall standing with no way in
but its gates; every quarry face stocked, and every face tile holding stone (`quarryFault` passes a tile that cannot hold it); every home tile playing by the same rules as colony 0's;
every paved road tile a road; nothing on the rock; the field ring and grove sealed from the town and
each other; every colony walkable from the first; every colony's walk to its nearest caravanserai
gate within 32 steps of every other's; wheat within 24 and wood within 32 of every colony. See
[generator verification](verification.md) for commands, revisions and evidence requirements.

## Implementation source

[CaravanseraiGenerator.cpp](../../src/map/generator/generators/CaravanseraiGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
