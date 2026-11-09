# Amphitheatre

A sunken arena in the middle of the map, ringed by walled terraces, and all round it a walled
territory for every colony with two inland seas beside its home. The outermost wall's ramps face the
colonies, the next wall's stand between neighbours, and so on down to the pit, so every step inward
is a meeting at a known place.

## Ground

Revision 6 draws every wall, coast and road from the terrain catalogue, where earlier revisions
stood the stone resource on grass and drew roads in sand. The design uses one terrain per rule, and a
last pass paints one of four climates (temperate, arid, highland, volcanic; `amphitheatre-climate`), the same
for every colony, with looks that keep those rules (`shared/Climates.h`). Walls are drawn on
vertices and lean away from the open ground beside them (`wallVertices`), so no lane or door
loses a tile to a wall. Since revision 8 every Ridges wall is at least three vertices thick
(`thickenRidges`), growing into open ground but never a lane, door or farm, because a ridge look
draws its peaks only on tiles with four rock corners. Barren and fertile ground take one look each,
no longer speckled with a second.
There are no beaches: grass meets the water, and a coast that must be sealed is cliff. Following the
maintainer's review (2026-11-07), sand appears only where the subject is sand, and a gate that must
stay unbuildable is rough ground.

| Ground | Rules | Where | Why |
| --- | --- | --- | --- |
| Rings | Ridges, every other ring in the climate's second rock | Every ring wall outside its ramps | The tiers read as steps; the towers at the outer wall cover the arena over it |
| Borders | Obstacles | Between territories, unclaimed ground, pockets a sea closed off | No tower shoots through a border (FEEDBACK 2026-09-14) |
| Ramps | Rough | Every ramp through every ring | Nothing can be built in a gate |
| Arena floor | Sand | The pit from three quarters of its radius to the first wall | Arena sand: a track round the orchard that cannot be fortified |
| Territory | Barren, paved round the swarm | Every territory but within ten steps of its seas, where its crops grow, and a grass verge four vertices wide at the foot of its walls | The base reads as the climate's ground; the verge holds the quarry faces and the towers |
| Lava | Lava | A pool at the middle of the pit, in the volcanic climate | Volcanic maps show lava at their centre |
| Deep water | Deep water | Each bay's water five vertices or more from land | Slower swimming, no algae |


- **Arena.** `rings` walls `terrace-width` apart round a pit of `pit-size`, drawn with
  `ringWithGates`; ramps `ramp-width` wide alternate between each colony's axis and the middle of
  its wedge.
- **Territories.** The ground outside the outer wall is shared out by `balancedTerritories` from every
  colony's ramp mouth: every tile goes to the colony whose mouth is nearest by squared distance less a
  weight per colony, and the weights are tuned until the areas are equal, so every border is a straight
  line between two neighbours.
- **Homes.** Every swarm stands the same number of steps from its ramp (`siteAtDepth`), four tenths
  of the way to the shallowest territory's far end; the wheat and wood kit faces the nearest sea, and
  two quarry faces of 12 stone each stand at the foot of the territory's side borders, as deep from
  the ramp as the swarm; since revision 7 every starting tower also has a face of 6 stone at the foot
  of the wall it stands against by the ramp, kept two steps from the ramp; scattered fields by
  `furnishGround`.
- **Inland seas.** `bay-size` percent of the smallest territory, split into two seas, one either side
  of the home (`growLakeBeside`), each wholly on its side of the line from the ramp through the home
  and ten tiles clear of it, seven from any wall. Where any territory lacks the room, every territory
  gets one sea of the full size at its far end instead (`growFarLake`). Every colony's seas are the
  same size; land a sea closes off becomes cliff (`strandedGround`). There is no sea shared between
  territories.
- **Prizes.** Orchards of the three fruits in the pit and on the innermost terrace, and an outcrop on
  every terrace, the same at every colony's angle.
- **Starting towers.** `tower-count` towers (default 3, at `starting-towers` level, default 1) and three open pads per colony in its own territory, each directly against the
  arena's outer wall within 18 steps of the colony's ramp mouth and clear of the ramp itself,
  covering the most of the arena (terraces, pit and ramps) over the outer ring (`chooseTowerSites`,
  `settleStartingTowers`). The starting towers protect the arena entrance; border
  defenses require further construction.
- **Checked, not assumed.** The terrain as designed, vertex for vertex, every ramp walkable, territories within
  `kAreaTolerance` percent, every colony's seas the same size, no territory reaching another or the
  arena with the outer ramps shut, and even walks to the ramps and to the pit.

## Implementation source

[AmphitheatreGenerator.cpp](../../src/map/generator/generators/AmphitheatreGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
