# Switchbacks

A plateau in the middle of the sea and a ring of homes on the rim, each joined to the plateau by a
mountain of stone with one zigzag trail through it. Walking the trail takes several times the
straight-line distance, but the walls between legs are thin, so the trail is a fortification: every
colony's towers stand on its own trail against the inner side of each wall and shoot across it at the
next leg up. Every home reaches a walled wheat farm on either flank.

- **Geometry.** Designed in every colony's `AxisFrame` and turned round the centre; round on a
  rectangular map. Homes stand on the rim; the mountain fills the ground between the plateau and the
  home with as many legs as fit (`layLegs`): every wall, between legs and at both ends, exactly
  `leg-wall` thick (default 2), and the trails widened to use up the rest, so the stone stays thin.
  On a crowded ring the plateau grows until the mountains fit round it; a smaller map narrows the
  trail and walls with the square root of its size.
- **Trail.** `zigzagPath` gives the trail and each leg's straight run; everything else in the
  mountain is stone. A sand road runs down the trail's middle.
- **Farms.** Every home reaches out across its axis to a field on either flank, and the fields share all
  the open sea between the mountains and round the rim by equal yield for their row angles
  (`growFarmFields` with no gap), joined to the home with no coast between. Then every tile of sea left
  is filled to its nearest field (`fillToNearest`, the whole map as its reach), so the farms always
  grow to fill the available space, and a single line of stone stands on every border between one
  colony's ground and another's (`labelBorders`): stone separates the colonies without wasting farmland on intervening water. Rows run across the axis at `bestFarmRows`
  widths (`layFarm`, rim 3), with a sand cap, a sand bridge clean across the whole farm every 16
  tiles (`water-crossings` and `crop-crossings`, both on, switch
  each half) and a 10x4 building plot under `farm-plots` (on);
  wheat with one woodlot (`plantFarm`).
- **Walls.** The mountains' rock, the border lines between colonies, and a sealed coast on whatever sea a
  design leaves (none at the defaults). Sea within a level-3 tower's range of the plateau becomes rock
  before the fill, so the mountains' inner ends join round the plateau and nothing outside the trails
  comes near it.
- **Homes and plateau.** Round homes (`stampRoundHome`) whose farms are their water on every size of
  map, with no starter kit of wheat and wood blocks: the farms on either flank feed
  the home, and `secureStartingCrops` supplies missing opening crops. The plateau has a pond and only fruit, an orchard of the three fruits between every
  two summits, so nothing overgrows it.
- **Starting towers.** `tower-count` (default 3) towers at `starting-towers` level
  (default 1; zero omits towers), and four open pads per colony, all on its trail
  beside the walkway down its middle, each directly against stone on the inner side of a wall - the side
  towards the middle of the map - so it shoots across the stone at the next leg up, where attackers
  coming down from the plateau pass (`chooseTowerSites` counting the colony's own trail). A trail too
  narrow for a tower beside its walkway gets fewer, the same for every colony, and no site may close a
  trail (`settleStartingTowers`).
- **Checked, not assumed.** Every designed stone present, every farm walkable from its colony
  (`farmReachable`), no land reachable from the sea, homes and their farms and the plateau apart with the
  trails shut and with only the middle legs shut (so no leg can be skipped), a level-1 tower at home
  reaching the first leg and one on the plateau reaching the last, and even walks to the plateau.

## Implementation source

[SwitchbacksGenerator.cpp](../../src/map/generator/generators/SwitchbacksGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
