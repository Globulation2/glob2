# Amphitheatre

A sunken arena in the middle of the map, ringed by walled terraces, and all round it a walled
territory for every colony with two inland seas beside its home. The outermost wall's ramps face the
colonies, the next wall's stand between neighbours, and so on down to the pit, so every step inward
is a meeting at a known place.

- **Arena.** `rings` walls `terrace-width` apart round a pit of `pit-size`, drawn with
  `ringWithGates`; ramps `ramp-width` wide alternate between each colony's axis and the middle of
  its wedge.
- **Territories.** The ground outside the outer wall is shared out by `balancedTerritories` from every
  colony's frontage (its wedge's arc just outside the wall): every tile goes to the colony whose frontage
  is nearest by steps less a weight per colony, and the weights are tuned until the areas are equal, so
  every border is a smooth curve of equal weighted distance between two frontages.
- **Homes.** Every swarm stands the same number of steps from its ramp (`siteAtDepth`), four tenths
  of the way to the shallowest territory's far end; the wheat and wood kit (no stone clump: the
  territory is walled in stone) faces the nearest sea; scattered fields by `furnishGround`.
- **Inland seas.** `bay-size` percent of the smallest territory, split into two seas, one either side
  of the home (`growLakeBeside`), each wholly on its side of the line from the ramp through the home
  and ten tiles clear of it, seven from any wall. Where any territory lacks the room, every territory
  gets one sea of the full size at its far end instead (`growFarLake`). Every colony's seas are the
  same size; land a sea closes off becomes stone (`strandedGround`). There is no sea shared between
  territories.
- **Prizes.** Orchards of the three fruits in the pit and on the innermost terrace, and an outcrop on
  every terrace, the same at every colony's angle.
- **Starting towers.** `tower-count` towers (default 3, at `starting-towers` level, default 1) and three open pads per colony in its own territory, each directly against the
  arena's outer wall within 18 steps of the colony's ramp mouth and clear of the ramp itself,
  covering the most of the arena (terraces, pit and ramps) over the stone (`chooseTowerSites`,
  `settleStartingTowers`). The starting towers protect the arena entrance; border
  defenses require further construction.
- **Checked, not assumed.** Every designed stone present, every ramp walkable, territories within
  `kAreaTolerance` percent, every colony's seas the same size, no territory reaching another or the
  arena with the outer ramps shut, and even walks to the ramps and to the pit.

## Implementation source

[AmphitheatreGenerator.cpp](../../src/map/generator/generators/AmphitheatreGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
