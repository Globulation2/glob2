# City states

A large shared commons in the middle of the map, ringed by a strait, and round it one big home for
every colony: a wedge of the outer land with its own lake, fields, quarry and paved town, cut off
from its neighbours by water channels and from the commons by the strait. Every home's sea coast is
a band of cliffs at the waterline with no beach, so the only way in or out of a home, walking or
swimming, is its causeway: a paved deck across the strait between rock parapets, landing on the
commons at the home's own angle. The commons is where the game is fought, richer towards its centre,
where an orchard of all three fruits stands at the heart. Swimming opens the commons' shores, the
channels and the archipelago at the wrap, never a home.

City states stays round on a rectangular map, in a circle on the shorter side: stretching it gives
the homes different shapes (deep and narrow along the long axis, wide and shallow across it), which
changes field area and fertility. Keeping a circle preserves comparable home geometry.

## Ground

Revision 14 (City states 2.0) builds every barrier and road from the terrain catalogue; earlier
revisions, written when the engine had only grass, sand and water, used the stone resource for walls
and sand for roads. The design is drawn with one terrain per rule (TerrainGroup.h), then a last
pass paints the climate's looks, which share those rules.
Since revision 15 the commons' ridges are at least three vertices thick (`thickenRidges`, growing
only into the commons' open grass), because a ridge look draws its peaks only on tiles with four
rock corners, and barren and fertile ground take one look each, no longer speckled with a second.

| Ground | Rules | Where | Why |
| --- | --- | --- | --- |
| Sea cliffs | Obstacles | Every home's land within two vertices of the sea, and a home creek's mouth where a bay brings the sea near | No beach to land on and no way out: the causeway is a home's only door, for swimmers too |
| Parapets | Ridges | Both sides of every causeway's deck | Walls the deck in; the gate's towers shoot over them along it |
| Causeway deck | Paths | Every causeway, paved between its parapets | Quick to cross; nothing grows on it |
| Gate throats | Rough | Four tiles of deck at each end of every causeway | Nothing can be built there, so no colony can close a gate with a building |
| Town | Paths | Round every swarm, from just inside the gate to a few tiles beyond the swarm and out to the side streets | Buildable, crops never grow into it, and it holds back no field beside it, as sand did |
| Streets and roads | Paths | Each home's main and side streets, the commons' roads from every landing to the heart and its ring road | As the town; one rule with it, so a street leaves no unbuildable seam through the town |
| Bridges | Paths | Every ford: the Riverside creek, the island heart, the delta | Land on all four corners of every ford tile |
| Home ground | Barren | Every home but within ten steps of its own water, the town and a grass verge four vertices wide at the foot of its cliffs | The base reads as the climate's ground; crops grow round the water, quarry faces stand on the verge |
| Islet plots | Barren | The ring round every islet's 10x4 building plot | Keeps the islet's wheat off the plot |
| Scrub | Barren | The scrub patches (`sand` control) on homes and commons | Holds crops back like sand, but can be built on |
| Rough cores | Rough | Two steps inside every scrub patch on the commons | Slow going and no building in the no-man's land; homes keep barren scrub, so building room is equal |
| Ridges | Obstacles | Highland homes' two ridges, broken by a pass | A home split into valleys |
| Crag | Ridges | The crag heart's ring, gapped towards every landing | Towers on the crag shoot over it |
| Citadel | Obstacles | The citadel heart's cliff ring, 4 tiles thick, with a 7-tile ramp towards every landing | A plateau with the orchard on top, held against arrows as well as feet |
| Lava | Lava | In the volcanic climate: the heart's water (the central lake, the island's moat, the crag's tarn, the delta's rivers) and a 4-tile moat round the citadel, broken at the ramps | Volcanic maps show lava at their centre; impassable on the ground, and fliers burn |
| Deep water | Deep water | Open sea five vertices or more from land | Slower swimming and no algae; algae stays in the shallows |

### Climates

One climate is drawn per map (`city-climate`), the same in every home. It paints looks only: each
look shares the rules of the terrain it replaces, checked when the generator compiles, so the four
climates play alike apart from the volcanic citadel's lava.

| Climate | Cliffs | Rock | Town / streets / roads / deck | Scrub / rough | Grass (homes / commons) | Sea |
| --- | --- | --- | --- | --- | --- | --- |
| Temperate | cliffs | ridge rock | brick path / brick path / overgrown road / slate ashlar | clay, heath / mudflat | lush / grass | deep water |
| Arid | sandstone benches | hoodoos | sandstone throughout, road on the commons | laterite, weathered sandstone / hardpan | grass / dry grass | deep water |
| Highland | granite tors | ridge rock | slate ashlar, road on the commons | shale, tundra / scree | grass / grass | deep water |
| Volcanic | obsidian ridge | outcrop | scoria blocks / basalt causeway | volcanic ash, dirt / pumice | grass / dry grass | dark water |

Bridges are boardwalk in every climate.
There are no beaches: grass meets the water, and a coast that must be sealed is cliff. Following the
maintainer's review (2026-11-07), sand appears only where the subject is sand, and a gate that must
stay unbuildable is rough ground.

## Design

- **Geometry.** A pure function of the request (`geometryFor`). The commons' radius is
  `commons-size` percent of half the shorter side, the strait `strait-width` percent of the shorter
  side, and the homes reach out to the map's half side less a rim of sea, so opposite homes never
  meet across the wrap. Both coasts are radial shapes (`coast-roughness`); the strait follows the
  commons' coast. Every home is the same wedge turned round the centre, so the layout is fair for
  any colony count. `validateRequest` needs at least 20 tiles of home between the strait and the
  sea, and at each home's inner coast at least the causeway plus ten tiles of arc beyond its
  channel.
- **Every roll differs.** Both coasts are random harmonic profiles periodic in the wedge: bays and
  headlands with a finer ripple on the commons' coast, which the strait follows, and bays into the
  flanks of every home's inner and outer coasts, all held flat around each causeway; every channel
  bows sideways by the same random amount. One climate, one home layout and one heart are drawn per
  map. Homes: Lakeland (one lake), Riverside (a creek from the lake towards one flank, bridged),
  Highland (two cliff ridges out to the sea with a pass each) and Marsh (four ponds). Hearts: a lake
  with the orchard on its shore, a rock crag with a gap towards every landing, an island in the lake
  reached by a bridge from every landing, a delta of rivers from the lake to the strait between the
  landings with a bridge each, a belt of forest round the orchard, and a citadel. Every home feature
  is designed in the wedge's frame, arc across it and radius out, and stamped into every home alike,
  so any roll stays fair by rotation.
- **Homes.** Each has one lake at six tenths of its depth (held clear of both coasts), the swarm
  between its causeway and the lake in a paved town, an identical unscaled kit of 40 wheat and 30
  wood beside the lake and a stone deposit beyond it, two quarry faces of 12 stone each at the foot
  of its sea cliffs either side of the town, then its own scaled ambient farmland on fertile ground,
  outcrops and a grove. The quarry faces replace the stone the old coast walls put beside every
  town: in paired games, the tower-building AIs mined 30-55% less stone without them.
- **Streets.** With `sand-roads` (on), a paved line two vertices thick runs from the heart of the
  commons along every home's axis, over its causeway, to a main street seven tiles inside the gate
  that follows the strait out to both flanks. Side streets turn inland from it past both sides of
  the swarm and at its ends. A ring road at half the commons' radius, or at the delta's bridges,
  joins every home's road. Streets keep five tiles from any water in a home and two on the commons,
  and stop where they would meet water, a ridge or a parapet.
- **Commons.** A central heart (above), `valleys` extra lakes (per 128×128 of commons) likelier
  towards the centre, farmland on fertile ground weighted by depth inward per `frontier-richness`,
  outcrops and groves by the same weight, and the orchard of the three fruits round the heart.
  Algae seeds the shallows. `secureStartingCrops` backs the kits up, causeways and approaches are
  cleared, and a cheapest-walk pass keeps a way open from every swarm to its causeway and from every
  landing to the heart.
- **The archipelago.** The sea outside the design circle includes the corners that wrap into one
  ocean and, on rectangular maps, the outer bands. Round islets of radius 11 dot it: one on the wrap
  point, then `islands` (0-4, 2) rings of eight, sixteen and so on at equal angles round it, at least
  two radii plus the moat apart (from each other's mirror images too), so the set has the map's own
  four-fold symmetry with mirrors. An islet whose disc and three tiles of water round it do not lie
  wholly in the sea is left out with its mirror images. Every islet carries the farms' 10x4
  building plot (`stampFarmPlot`) at its middle; wheat fills every other plantable tile,
  independently of the resource amount controls. An islet is only ever reached by swimming from the
  commons: a forward post, not a stepping stone. The archipelago cannot be turned round the centre
  for every colony, so with three, five or six colonies it lies nearer some homes than others, which
  the lobby's best-of-five rolls cover, as on Canals.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every causeway deck and
  bridge walkable, every parapet vertex rock, every colony and the heart reachable on foot from
  colony 0, every islet's plot buildable and no islet reachable on foot. With every causeway shut, no
  colony may walk out of its home and, with the walls on, no colony may walk or swim out of it
  either. The colonies' walks to their landings must be within twelve steps of each other.

## Controls

| Control | Default | Range | Effect |
| --- | --- | --- | --- |
| Commons size | 55 | 30–65 | The commons' radius as a share of half the shorter side |
| Strait width | 4 | 3–14 | The strait's width as a share of the shorter side |
| Causeway width | 7 | 5–11 | Each causeway's deck, in tiles |
| Coast roughness | 50 | 0–100 | Bays and headlands on every coast and the bow in the channels |
| Valleys | 3 | 0–8 | Extra commons lakes per 128×128 of commons |
| Scrub patches (`sand`) | 3 | 0–8 | Scrub patches per 64×128 of land |
| Islands | 2 | 0–4 | Rings of islets round the wrap point |
| Frontier richness | 60 | 0–100 | How much richer the commons' heart is than its shores |
| Cliff walls (`stone-walls`) | on | | Off: no sea cliffs, parapets or quarry faces; the homes' coasts are open |
| Paved roads (`sand-roads`) | on | | Off: no streets (towns and causeway decks stay paved) |
| Resource amounts | 100% | 0–300% | Every home's ambient fields, outcrops and grove, and everything on the commons; every home's kit, its quarry faces and the islets' wheat are unscaled |

The control ids are those of earlier revisions, so saved requests and the online pool keep working.

## Evidence

Revision 14 was compared with revision 12 in paired 30,000-tick games (the same ten map seeds,
Nicowar, Cortex, Cabino and Maxima in fixed seats, 256×256): no colony starved out or lost its
economy, and the differences between AIs moved by more between two game seeds of the same maps than
between the revisions. Paving the towns and roads in place of sand raised the wheat harvest at the
same stage of the work (sand held back the fields beside it; paved ground does not, and hauling on
it is quicker), so colonies grow somewhat faster than on revision 12, which changes pacing.

## Implementation source

[CityStatesGenerator.cpp](../../src/map/generator/generators/CityStatesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
