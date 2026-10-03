# Farm areas

Farm areas are an [experimental feature](experimental-features.md) with the key
`farm-areas`, off by default. A game that carries it offers a fourth painted area
next to forbidden, guard and clearing areas: a per-team bitmask on every tile
(`Tile::farmArea`), painted with the same brush and add/remove modes, and saved
with the map from format 130. Without the experiment the brush has three zones,
the farm order is refused, and a farm mask that reaches the game some other way
(a map or save made with it) is inert.

A farm area changes **where a harvest takes its resource from**, and makes the
farm keep itself clear of what it does not grow.

## What players see

- Switch on **Settings → Experiments → Farm areas**, then start or host a new
  game. The flag panel's zone strip gains a fourth, green button; the touch brush
  bar and flag palette gain a **Farm** choice. `<a>-<w>` selects it from the keyboard.
- With the setting on, the map editor (desktop and phone) also offers the farm
  brush, with the same refusal of ground that cannot grow. Maps do not carry
  experiments: a painted farm only takes effect in games that have the experiment.
- The brush refuses ground nothing can grow on. A farm on grass is a wheat farm
  and a farm on water is an alga farm; the terrain decides, so there is no extra
  setting.
- Workers harvest from wherever they reach the field, but the grain comes off its
  ripest tile, so the seedlings at the edge they stand next to survive. The field
  keeps one grain on every tile, and trees growing into it are cut down.

## The rule

When a worker's harvest completes on a tile inside its own team's farm area, and
the resource is farmable:

1. **The field** is every tile holding that resource reachable from a tile of the
   worker's own 3x3, through 8-connected tiles that also hold it, without leaving
   the painted area.
2. One unit comes off the tile of that field holding the **most**. Ties break by
   distance to the worker, then by tile index, so every client and replay agrees.
3. A tile at one grain is that field's **seed** and is never the source.
   `decResource` clears a granular tile at its last grain, so without this a farm
   could be worked down to bare ground. A field worked past its surplus stalls at
   one grain a tile and regrows.
4. If nothing in the field is above its seed, the worker gets **nothing** and goes
   back to looking instead of walking home empty-handed. Because a seed tile still
   holds wheat, it harvests again after the animation, so workers wait at a
   flattened farm until it regrows.

Seeding the flood from the worker's 3x3 rather than its target tile keeps a
harvest working when the target empties during the animation, and connectivity
runs through the resource, so an empty gap ends the field: wheat across it is
never taken.

Outside a farm area, and in every game without the experiment, nothing changes,
including the old behaviour that a harvest completing on a tile that emptied
under the animation still grants a resource.

## Why

Wheat is granular: a tile holds up to five grains and regrows. Every wheat tile is
an equal goal of the resource gradient, so a crowd of workers converges on the
nearest one, usually a fresh one-grain seedling at the near edge, and strips it.
The field is eaten from its rim inwards and cannot recover, because only a tile
that already holds the resource grows or seeds a neighbour (`Map::growResources`).
Players work around this by painting forbidden checkerboards.

With a farm area the rim tile a worker stands next to is no longer the one that
loses a grain, so edge seedlings survive and ripen. A ground unit cannot stand on a
resource tile (`Map::isFreeForGroundUnit`) and blocks growth into the tile it
stands on (`Map::incResource`), so workers ringing the field occupy the tiles it
would expand into. A field flattened by heavy harvesting also spreads more slowly,
since a sampled tile seeds a neighbour with probability amount/8. The cost of
harvesting hard is therefore paid in the field's footprint rather than its stock.
This trade is read off the growth code and has not been measured; a yield versus
worker-count curve is the obvious next measurement.

## Keeping the farm clear

`Map::isClearingTarget` makes any clearable resource inside a farm area a clearing
goal unless it is the crop that tile's terrain grows. Wood creeping into a wheat
farm is cut down, and the field grows into the forest as the trees fall. It is the
predicate the clearing-area gradient and `Unit::tryClaimClearingAreaForHarvesting`
already used, widened by one case, so the existing claim bookkeeping still stops two
workers taking the same tile. Where a clearing area and a farm overlap, the
clearing area wins: everything clearable there is cleared, the crop included.

## Which resources

The rule applies to a resource that is granular, shrinkable, expendable and not
eternal: **wheat and algae**. Wood is not granular (one harvest clears the tile),
so a farm over a forest changes nothing about which tree falls. Stone and the
fruits are eternal.

## Which ground

`Map::canPaintFarmArea` refuses tiles the map forbids growth on, sand and other
terrain with no farmable crop, tiles holding stone, papyrus or fruit, and tiles too
far from water for the growth probe ever to succeed (it draws offsets in [-15,15]
and needs water there; algae also need sand at twice the offsets). The refusal is
in `Game::executeAlterFarmArea`, the path a replay and every remote client take;
the brush preview applies the same predicate so the overlay matches what lands.
A refused tile is never part of a field, so it cannot connect two patches.

## What it does not change

- **The resource gradient.** Workers walk to the nearest tile holding the resource
  by the same field; only the moment the harvest completes differs.
- **Growth.** `Map::growResources` does not read the farm mask.
- **Clearing the touched tile.** Clearing paths call `Map::decResource` on the
  tile they touch and never go through `takeHarvest`.

## Artwork

The overlay marker and its zone button are hand-authored SVG
(`datasrc/gfx/authored/area-farm.svg`, `gamegui58.svg`): sparse seedlings that
sway over eight frames, in the same family as the guard dots and clearing sickles.
`python3 tools/artwork/render_authored.py` renders the classic 32px sprites and the
128px high-resolution frames from the same source; `--check` verifies both are
current and follow the marker rules. Zoomed out, the farm fades to the same flat
tint as the other zones, a light green chosen to stand out from grass.

## AIs

In a game with the experiment, every AI that farms wheat paints a farm area
instead of its forbidden-zone pattern: Nicowar, Econo, Maxima, Cortex, Cabino and
Warrush. Each keeps its own idea of which fields to farm (near water, within its
managed territory) and adds the shared rule `AIFarmAreas::wantsFarm`
(`src/ai/AIFarmAreas.h`): ground the farm can grow on that holds wheat or touches
it, so the farm covers the field and the ring it grows into. Forbidden paint stays
for wood outside farms, and an AI removes its old wheat paint when it switches.
Cortex erases its farm for a wheat blitz, and Maxima when farming is disabled.
Without the experiment every AI farms exactly as before.

## Scripts

JavaScript AIs and map scripts see the experiment through
`ctx.game.experiments()`, paint and erase farms with the `farmArea` order (same
fields as the other area orders), and read `farmArea: true` on own-team farm
tiles. See the [JavaScript API](../development/javascript-api.md).

## Compatibility

Save format 130 (`FILE_FORMAT_VERSION_FARM_AREA`) adds the mask to the map
section, packed or per tile; older maps and saves load with no farm painted.
`Tile::farmArea` joins the heavy `Map::checkSum`, which is unchanged while it is
zero. The order is `ORDER_ALTER_FARM_AREA` (45); `OrderValidation` rejects it as
`not_permitted` in a game without the experiment and `Game::executeAlterFarmArea`
ignores it there. Network protocol 52 came with the format change, and each
change to what the experiment simulates (the rule, scripts, the AIs) bumps
`SIM_REVISION` like any other simulation change. Replays from formats 127 to 129 still play: none can contain the
farm order or this experiment.

## Where it lives

| Concern | Code |
| --- | --- |
| Experiment registration | `src/ExperimentalFeatures.*` (`ExperimentId::FarmAreas`) |
| Tile mask, rule and clearing predicate | `src/map/Map.h`, `src/map/MapResources.cpp` |
| Harvest call site | `src/unit/UnitDisplacement.cpp`, `DIS_HARVESTING` |
| Clearing | `src/map/gradient/MapGradientArea.cpp`, `src/unit/UnitMovement.cpp` |
| Painting it | `OrderAlterFarmArea`, `OrderValidation.cpp`, `Game::executeAlterFarmArea` |
| Save format | `src/map/io/MapIO.cpp` |
| Desktop panel, touch UI and overlay | `src/gui/GameGUIInternal.h` (`zoneStripButtonX`), `GameGUIToolManager`, `GameGUITouch*.cpp`, `src/render/GameRenderTerrain.cpp` |
| Tests | `test/FarmAreaTest.cpp` (`FarmAreas/*`) |

Design discussion: [Globulation2/glob2#271](https://github.com/Globulation2/glob2/pull/271)
and the original [pull request #277](https://github.com/Globulation2/glob2/pull/277).
