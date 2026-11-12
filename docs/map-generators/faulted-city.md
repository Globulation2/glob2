# The Faulted City

The Faulted City (`faulted-city`, numeric ID 64) is a planned city broken by an earthquake into
displaced quarters. One source city is drawn once, then sampled through a separate integer
translation in each district of a coarse, warped toroidal tessellation. Source block identities
persist across the faults, so streets and block outlines continue at different offsets on
opposite sides. The map fills the torus, including its seams.

Revision 6 (Faulted City 2.0, November 2026) draws the city from the terrain catalogue in one
fixed urban look. Revisions 1 to 5 used grass, sand and water with stone-resource masonry:
- the faults were open sand;
- the streets were grass, indistinguishable from the lawns;
- the gardens were sand-ringed, behind beaches.

## Rifts, scarps and ramps

Every district stands at its own random height. Along every fault, the higher district's rim is a
**scarp** of Ridges rock, three or four tiles deep and frayed by noise. Units cannot cross a scarp,
but towers shoot over it. The rest of the fault is a **rift floor** of rough rubble (scree): it is
walkable at about 0.6 speed and cannot be built on. Some faults hold a **sag pond** at their middle,
where the floor is broad enough to keep two tiles of floor round it.

A district is entered across its scarps only by paved **ramps**. A ramp opens where a source
avenue meets the district's own scarp: every fault tile within six tiles of the gate is paved on
all four corners, so the ramp keeps its whole width. Each district with a scarp has two ramps,
plus zero, one or two more at Few, Normal or Many surviving junctions; they are spread round the
district. A district's low sides open straight onto the rift floor.

The ramps are therefore the city's fronts. A raised quarter (high on every side) is a fortress
whose only entrances are its ramps. A sunken quarter (low on every side) is open to the rift all
round.

## Streets, facades and ruins

- **Streets** are paved: twice as fast to walk, still buildable, and nothing grows on them. A paved
  street therefore seals every block from its neighbours.
- **Facades.** The surviving facades of civic blocks, the hall frontages round reclaimed ruins and
  the plaza's colonnades are Ridges walls, drawn on vertices that lean away from the streets and
  thickened to three vertices so they read as masses.
- **Collapsed courtyards** (empty ruin shells) keep their stone-resource walls. They are rubble a
  colony can quarry, and their courtyards hold rubble heaps scaled by the stone amount.
- **Gardens.** Caps and dividers are barren clay: it contains crops as the old sand did, but can be
  built on.
- **Water.** No beaches are laid, so grass meets water directly.

Civic blocks are:
- markets: a paved square with a round fountain, and a garden behind;
- productive gardens;
- reservoirs with an inn service court;
- orchards.

## Home blocks

Colonies start in stamped **home blocks**: source blocks that lie whole inside one district, at
least three tiles clear of its faults and of the plaza. One of three designs is drawn per map,
with one facing per map, so every home is an exact translation of every other:

| Design | Layout |
| --- | --- |
| Forum | A paved court and a lawn, and a farm along the block's back with wheat and wood either side of a divider, watered by a pond row |
| Cloister | A paved court down the middle, a wheat garden along a canal on one side, and a wood garden along another below the lawn |
| Terrace | A paved court in one corner and a lawn beside it, and a long farm whose pond parts wheat from wood |

Each home also has a remnant wall of Ridges rock on its lawn, with a quarry face of 12 stone at its
foot. The swarm stands in the court. Several spread arrangements of home blocks are compared with
the start-quality model, and the team indices are dealt at random.

## The plaza

A three-block civic plaza crosses an interior fault, so its two pieces stand in different
districts. Its pieces are:
- a paved promenade with fountains (arcade, twin-fountain or market variant);
- a fruit avenue either side;
- paved walks;
- a grass verge with broken colonnades;
- a quarry face of 16 stone at the colonnades' feet.

At least half the avenue's trees are planted whatever the fruit amount, and every tree from 100%.
Colonnades must survive in at least two districts. Telemetry records each colony's walk to the
plaza.

## Controls

- **Fault displacement:** maximum translation on either axis, 4–14 tiles, default 10.
- **Fault width:** the rift's radius is half this corner width, 8–16, default 12.
- **Ruin density:** percentage of source blocks assigned ruins, 20–60%, default 35%.
- **Surviving junctions:** ramps per district with a scarp: two, plus zero, one or two
  (Few, Normal, Many). Normal is the default.
- **Resource amounts:**
  - Every renewable tile of each colony's starter wheat and wood plot is seeded. Other plots fill to
    80% of renewable capacity at 100%, and to full capacity at 300%.
  - Stone scales the rubble heaps. The collapsed courtyards' shells and the home and plaza quarry
    faces are structural and stay at zero.
  - Fruit scales the orchards, while the plaza avenue keeps at least half its trees.

Dimensions are 256 or 512 tiles on each axis, with 1–16 colonies; counts above twelve require
512×512. The generator searches up to eight deterministic city layouts before refusing a request.
Layout and home selection use fixed maximum resource amounts, so resource sliders never redraw the
terrain.

## Economy and fairness

- **Containment.** Paved streets, barren caps and dividers contain every garden, and
  `containedPlotsMismatch` proves the seal on the finished map. Generated maps never disable growth.
- **Fertility** is measured on the final terrain with the catalogue's rules.
- **Starter checks.** Each home's starter wheat plot must have at least 48 fertile tiles and three
  units of summed growth probability. A completed colony needs:
  - 12 wheat deposits within 12 steps and 24 within 24, from its own starter plot;
  - its own starter wood within 24 steps;
  - six separate reachable four-by-four building footprints outside future crop growth.
- **Food.** On six 256×256 four-colony seeds, the growth potential of sealed plots within 24 steps
  of a home rose by about half against revision 5, and within 48 steps by about three quarters.
  Fast streets, rough rifts and stone moved from masonry to quarry faces and rubble change the
  pace of play.

Fairness is by construction for the homes: one design and facing are stamped identically. The
city round them and the walk to the plaza are fair by measurement only. AI games and human review
remain necessary to judge the ramps, the raised and sunken quarters, and the plaza.

## Validation

`validateWorld` rebuilds the plan from the request. It finds each colony's home from its workers,
then redraws the city and compares every terrain vertex with the design. It also checks:
- the scarps and facades by group;
- the collapsed courtyards' stone, and equal quarry faces in every home;
- that no rift floor holds a deposit;
- that every ramp's approach is walkable;
- that every occupied quarter has two ways onto the rift (ramps or a low side);
- the starter food, wood, stone and building room;
- that every ramp is reachable from the first colony.

## Verification

The source definition and its request and world validators define the accepted settings and the
finished-map guarantees. Run the focused `MapGeneratorDefaults` "Faulted City contracts" case and
the sim-diff map fingerprints. Inspect small and large rectangles, every home design and the
resource extremes in the game view (`glob2 map render`), then check populated games with complete
seat rotations. See [generator verification](verification.md) for commands, revisions,
save/replay checks and evidence requirements.

## Implementation source

[FaultedCityGenerator.cpp](../../src/map/generator/generators/FaultedCityGenerator.cpp) owns this
landscape's construction, controls and validation. See the [catalog](catalog.md) for its stable
command and legacy IDs.

Related: [map generators](README.md).
