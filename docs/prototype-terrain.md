# Prototype terrain: ice and cobblestone

Two undermap terrain types beyond water, sand and grass, added to try their game rules
before investing in artwork. Both are off in every existing map and generator default:
a map without them simulates, generates and saves exactly as before.

| | Ice (`ICE`, 3) | Cobblestone (`COBBLESTONE`, 4) |
| --- | --- | --- |
| Walking and swimming speed | halved | doubled |
| Pathfinding step cost (land is 10) | 30, so routes avoid it when a detour is short | 5 |
| Idle wandering | never steps onto it | as grass |
| Harm | ground units outside buildings lose 1 HP every 32 ticks; explorers are unaffected | none |
| Buildings | no | yes |
| Resources | none placed or grown | none placed or grown |
| May touch | anything | anything but water |

Rules live in `Map::stepCost`/`minStepCost` (`src/map/gradient/MapGradientField.cpp`),
`Unit::terrainSpeed` and `Unit::takeTerrainDamage` (`src/unit/`), `Map::checkTile`
(`src/map/MapQuery.cpp`) and the tunables at the end of `src/unit/UnitConsts.h`. Deaths on ice
count under the "unknown" cause: a new cause would change the saved measurement layout.
The A* bound drops to the cobblestone step only on maps that contain cobblestone, so other
maps search exactly as before.

## Tiles

A tile is drawn from its four undermap corners. A tile with any ice corner is ice (sprites
272-287), one with four cobblestone corners is cobblestone (288-303); any other tile uses the
original grass/sand/water lookup with cobblestone corners read as sand. Both ranges fail
`isGrass`, `isSand` and `isWater`, which is what keeps resources off them. The sprites are
flat placeholders drawn by `tools/placeholder_terrain.py`; real artwork can replace
`data/gfx/terrain272.png`-`terrain303.png` under the same names, but proper transitions need a
renderer that blends terrain borders.

Grass never touches water (sand lies between), so a rule keeping ice off sand would leave
no valid way to lay ice across a sand-banked river, even allowing diagonal contact; ice
therefore touches anything.

## Editing and generators

The map editor's terrain panel has Ice and Cobblestone brushes; painting cobblestone beside
water (or water beside cobblestone) turns the water corner to sand. Two generators show the
terrains off:

- **Watershed**, *Frozen crossings*: Half frozen lays every other ford in ice, All frozen every
  ford.
- **City states**, *Road surface*: Cobblestone paves the sand roads.
- **Old town**, *Cobblestone streets*: the city's streets are paved, leaving a grass verge
  along every block so the blocks keep their stone.
- **Fjord continent**, *Ice bridges*: ice spans the middle of every fjord from grass to grass,
  a short way to a neighbour besides the walk round through the core.

## Compatibility

Old saves, replays and maps load unchanged. `VERSION_MINOR` is not bumped yet: a map
containing the new terrain opened by an older client would hit the renderer's unknown-tile
assertion, so the bump belongs with merging.
