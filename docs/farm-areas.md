# Farm areas

A farm area is the fourth painted area, next to forbidden, guard and clearing
areas. It is a per-team bitmask on every tile (`Tile::farmArea`), painted with
the same brush and the same add/remove modes, and saved with the map from
format version 98.

It changes one thing: **where a harvest takes its resource from.**

## The rule

When a worker's harvest animation completes on a tile inside its own team's
farm area, and the resource is farmable:

1. **The field** is every tile holding that resource which is reachable from a
   tile of the worker's own 3x3, through 8-connected tiles that also hold it,
   without leaving the painted area.
2. One unit of the resource comes off the tile of that field holding the
   **most** — ties broken by distance to the worker, then by tile index, so the
   choice is the same on every client and in every replay.
3. If the field is empty, the worker gets **nothing** and goes back to looking.

Outside a farm area nothing changes, including the long-standing behaviour that
a harvest completing on a tile that emptied under the animation still hands the
worker a resource. That is deliberate: a game with no farm area painted
simulates identically to one without this feature, which is checkable with
`GLOB2_CHECKSUM_SIDECAR`.

## What it is for

Wheat is granular: a tile holds up to five grains and regrows. Without a farm
area every wheat tile is an equal goal of the resource gradient, so a crowd of
fetchers converges on the nearest one — usually a fresh one-grain seedling at
the near edge of the field — and strips it. The field is eaten from its rim
inwards and stops being able to regrow, because only a tile that already holds
the resource can grow or seed a neighbour (`Map::growResources`).

Pooling the harvest over the connected field fixes that without a ripeness
floor, a fetcher cap or per-tile claims:

- The rim tile a worker stands next to is not the tile that loses a grain, so
  seedlings at the edge survive and ripen.
- Workers therefore stop at the outer rim and do not walk into the field, which
  matters because a ground unit cannot stand on a resource tile at all
  (`Map::isFreeForGroundUnit`) and does block growth into the empty tile it is
  standing on (`Map::incResource`).
- A harvest that finds nothing hands out nothing, so the field cannot produce
  grain that was never there.

The cost of harvesting hard is paid in the field's **footprint**, not its
stock. A sampled source tile adds one grain either in place or to a neighbour,
with the spread probability equal to its amount over eight, so a field flattened
by heavy harvesting expands at a fifth of the rate of a ripe one — and the ring
of workers standing around it occupies the very tiles it would expand into. The
decision the player makes is how big to paint the farm and how many workers to
point at it.

## Which resources

The rule applies to a resource that is granular, shrinkable and expendable:
**wheat and algae**. Both accumulate on a tile, both can be used up, and both
regrow in place and seed their neighbours (algae need water and a coast, which
is why a coastal strip harvested to its last tile never comes back — nothing
reseeds an empty region).

Wood is **not** granular: one harvest clears the whole tile, so a wood tile is
never part of a shared stock and a farm area painted over a forest changes
nothing. Stone and the fruits are eternal and never need protecting.

## What it does not change

- **The resource gradient.** Workers still walk to the nearest tile holding the
  resource, by the same field, with the same goals. Only the moment the harvest
  completes is different.
- **Growth.** `Map::growResources` does not read the farm mask.
- **Clearing.** A clearing-area harvest still takes the tile it is aimed at.
- **The AIs.** Nicowar still paints forbidden checkerboards near water in its
  farming phase; nothing yet paints a farm area.

## Where it lives

| Concern | Code |
| --- | --- |
| Tile mask and accessors | `src/map/Map.h` (`Tile::farmArea`, `isFarmArea`) |
| The rule | `src/map/MapResources.cpp` (`isFarmableResource`, `pickFarmHarvestTile`, `takeHarvest`) |
| Harvest call site | `src/unit/UnitDisplacement.cpp`, `ACT_FILLING` / `DIS_HARVESTING` |
| Painting it | `OrderAlterFarmArea`, `Game::executeAlterFarmArea` |
| Save format | `src/map/io/MapIO.cpp`, gated on `FILE_FORMAT_VERSION_FARM_AREA` |
| Panel and overlay | `src/gui/GameGUIInternal.h` (`zoneStripButtonX`), `src/render/GameRenderTerrain.cpp` |
| Map editor | `src/map/edit/` (`FarmAreaBrush`, `ZoneSelector::FarmingZone`) |
| Tests | `test/FarmAreaHarvestHarness.cpp`, built by `scons release=1 server=0 farm-test` |
