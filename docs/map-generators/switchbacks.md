# Switchbacks

A plateau in the middle of the sea and a ring of homes on the rim, each joined to the plateau by a
mountain of rock with one zigzag trail through it. Walking the trail takes several times the
straight-line distance, but the walls between legs are thin, so the trail is a fortification: every
colony's towers stand on its own trail against the inner side of each wall and shoot across it at the
next leg up. Every home reaches a walled wheat farm on either flank.

## Ground

Revision 7 draws every wall, coast and road from the terrain catalogue, where earlier revisions
stood the stone resource on grass and drew roads in sand. The design uses one terrain per rule, and a
last pass paints one of four climates (temperate, arid, highland, volcanic; `switchbacks-climate`), the same
for every colony, with looks that keep those rules (`shared/Climates.h`). Walls are drawn on
vertices and lean away from the open ground beside them (`wallVertices`), so no lane, door or home
loses a tile to a wall.
There are no beaches: grass meets the water, and a coast that must be sealed is cliff. Following the
maintainer's review (2026-11-07), sand appears only where the subject is sand, and a gate that must
stay unbuildable is rough ground.

| Ground | Rules | Where | Why |
| --- | --- | --- | --- |
| Mountain core | Ridges, banded leg by leg in two rocks | Within the outermost legs' reach of the mountain's axis: the walls between legs (4 tiles by default, so the rock shows its spires; a crowded map thins them) and the thinner walls at both ends of the trail | The firing line: towers on the trail shoot over it at the next leg up, the home's at the first leg and the plateau's at the last |
| Mountain flanks | Obstacles | The rest of every mountain | Solid rock no tower shoots through |
| Plateau guard and borders | Obstacles | Sea near the plateau (`kPlateauGuard`), every border between colonies | Nothing outside shoots into the plateau, or across a border |
| Sea cliffs | Obstacles | Any coast of open sea (none at the defaults) | No beach to land on |
| Trail | Paths | Every trail, from wall to wall | A quicker climb; the towers keep off its walkway |
| Farms | Grass in the climate's farm look; crossings Paths, caps and plot rings Barren (`dressFarmSand`) | Every farm | Farm ground reads by climate; no sand stands in for a road or a ring |
| Base | Barren, paved round the swarm | Every home, but a grass verge four vertices wide at the foot of its walls | Food grows on the farms, so the base is the climate's barren ground; the verge holds the quarry faces and the towers against the walls (a tile mixing grass and barren corners holds no building) |
| Lava | Lava | The plateau's pond, in the volcanic climate | Volcanic maps show lava at their centre |

Every home has two quarry faces of 12 stone each at the mountain's foot either side of its trail's
mouth: the mountains were its stone when they were built of it. They are unscaled.


- **Geometry.** Designed in every colony's `AxisFrame` and turned round the centre; round on a
  rectangular map. Homes stand on the rim; the mountain fills the ground between the plateau and the
  home with as many legs as fit (`layLegs`): every wall, between legs and at both ends, exactly
  `leg-wall` thick (default 2), and the trails widened to use up the rest, so the rock stays thin.
  On a crowded ring the plateau grows until the mountains fit round it; a smaller map narrows the
  trail and walls with the square root of its size.
- **Trail.** `zigzagPath` gives the trail and each leg's straight run; everything else in the
  mountain is rock. The trail is paved from wall to wall.
- **Farms.** Every home reaches out across its axis to a field on either flank, and the fields share all
  the open sea between the mountains and round the rim by equal yield for their row angles
  (`growFarmFields` with no gap), joined to the home with no coast between. Then every tile of sea left
  is filled to its nearest field (`fillToNearest`, the whole map as its reach), so the farms always
  grow to fill the available space, and a single wall of cliff stands on every border between one
  colony's ground and another's (`labelBorders`): it separates the colonies without wasting farmland on intervening water. Rows run across the axis at `bestFarmRows`
  widths (`layFarm`, rim 3), with a sand cap, a sand bridge clean across the whole farm every 16
  tiles (`water-crossings` and `crop-crossings`, both on, switch
  each half) and a 10x4 building plot under `farm-plots` (on);
  wheat with one woodlot (`plantFarm`).
- **Walls.** The mountains' rock, the border lines between colonies, and a sealed coast of cliff on whatever sea a
  design leaves (none at the defaults). Sea within a level-3 tower's range of the plateau becomes rock
  before the fill, so the mountains' inner ends join round the plateau and nothing outside the trails
  comes near it.
- **Homes and plateau.** Round homes (`stampRoundHome`) whose farms are their water on every size of
  map, with no starter kit of wheat and wood blocks: the farms on either flank feed
  the home, and `secureStartingCrops` supplies missing opening crops. The plateau has a pond and only fruit, an orchard of the three fruits between every
  two summits, so nothing overgrows it.
- **Starting towers.** `tower-count` (default 3) towers at `starting-towers` level
  (default 1; zero omits towers), and four open pads per colony, all on its trail
  beside the walkway down its middle, each directly against rock on the inner side of a wall - the side
  towards the middle of the map - so it shoots across the rock at the next leg up, where attackers
  coming down from the plateau pass (`chooseTowerSites` counting the colony's own trail). A trail too
  narrow for a tower beside its walkway gets fewer, the same for every colony, and no site may close a
  trail (`settleStartingTowers`).
- **Checked, not assumed.** The terrain as designed, vertex for vertex, every farm walkable from its colony
  (`farmReachable`), no land reachable from the sea, homes and their farms and the plateau apart with the
  trails shut and with only the middle legs shut (so no leg can be skipped), a level-1 tower at home
  reaching the first leg and one on the plateau reaching the last, and even walks to the plateau.

## Implementation source

[SwitchbacksGenerator.cpp](../../src/map/generator/generators/SwitchbacksGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
