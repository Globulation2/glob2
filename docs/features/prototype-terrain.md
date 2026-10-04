# Prototype terrain: ice and cobblestone

Two undermap terrain types beyond water, sand and grass, added to try their game rules
before investing in artwork. Both are off in every existing map and generator default:
a map without them simulates, generates and follows the same terrain rules as before.

| | Ice (`ICE`, 3) | Cobblestone (`COBBLESTONE`, 4) |
| --- | --- | --- |
| Walking and swimming speed | halved | doubled |
| Pathfinding step cost (land is 10) | 30, so routes avoid it when a detour is short | 5 |
| Idle wandering | never steps onto it | as grass |
| Harm | ground units outside buildings lose 1 HP every 32 ticks; explorers are unaffected | none |
| Buildings | no | yes |
| Resources | none placed or grown | none placed or grown |
| May touch | anything | anything but water |

Rules live in `Map::stepCost`/`minStepCost` (`src/map/gradient/MapGradientDirection.cpp`),
`Unit::terrainSpeed` and `Unit::takeTerrainDamage` (`src/unit/`), `Map::checkTile`
(`src/map/MapQuery.cpp`) and the tunables at the end of `src/unit/UnitConsts.h`. Deaths on ice
count under the "unknown" cause: a new cause would change the saved measurement layout.
The A* bound drops to the cobblestone step only on maps that contain cobblestone, so other
maps search exactly as before.

## Tiles

For the rules, a tile with any ice corner is ice (sprites 272-287) and one with four
cobblestone corners is cobblestone (288-303); any other tile uses the original
grass/sand/water lookup with cobblestone corners read as sand. Both ranges fail `isGrass`,
`isSand` and `isWater`, which is what keeps resources off them.

Drawing works from the corners instead (`Map::prototypeTerrainLayers`, extracted into `SceneMap` for
`Game::drawMapTerrain`): a tile touching ice or cobblestone first draws the ground beneath, the
original tile its other corners make, then lays a cobblestone and then an ice edge sprite over
it (304-415 ice, 416-527 cobblestone: 14 corner shapes, 8 variants). The edge sprites are cut
to the alpha of the sand-over-water tiles of the same shape, so they meet other terrain with a
beach's ragged outline. Variants come from the tile position, never the synchronized RNG, so
drawing changes nothing in the simulation. Prototype scenes bypass the flat-tile software and
OpenGL terrain caches so that single-corner overlays remain visible; this can increase
rendering cost on those maps.

`tools/placeholder_terrain.py` draws all of these from the game's own art: ice is the water
tiles recoloured pale with faint cracks, cobblestone is rounded stones carrying the sand
tiles' grain. Better artwork can replace the files under the same names.

Grass never touches water (sand lies between), so a rule keeping ice off sand would leave
no valid way to lay ice across a sand-banked river, even allowing diagonal contact; ice
therefore touches anything.

## Editing and generators

The map editor's terrain panel has Ice and Cobblestone brushes; painting cobblestone beside
water (or water beside cobblestone) turns the water corner to sand. Four generators show the
terrains off:

- **Watershed**, *Frozen crossings*: Half frozen lays every other ford in ice, All frozen every
  ford.
- **City states**, *Road surface*: Cobblestone paves the sand roads.
- **Old town**, *Cobblestone streets*: the city's streets are paved, leaving a grass verge
  along every block so the blocks keep their stone.
- **Fjord continent**, *Ice bridges*: ice spans the middle of every fjord from grass to grass,
  a short way to a neighbour besides the walk round through the core.

Automatic map searches retain the existing terrain defaults; these prototype options must
be selected explicitly.

## AI awareness

Only the basics: every AI that places buildings through `Map::checkTile` sees cobblestone as
buildable, as do Castor's building-space map and Maxima's buildable-ground counts
(`Map::isBuildableGround`). Castor's own walking map treats ice as a wall. Route-finding keeps
every AI's units off ice when a detour is short. No AI plans around ice damage or seeks roads out.

## Compatibility

Existing save formats remain readable down to minor 58; the replay floor remains 127.
Minor 134 permits the new undermap and rendered tile IDs and reconstructs derived terrain
counts. Minor 133 and older loads retain their original tile bounds. Simulation revision 16
separates clients under the current online simulation-version contract.

On prototype maps, eager fields, resumed building searches and asynchronous global fields
share mixed movement costs. Pending jobs retain a frozen movement snapshot; save/load
retains completed pending publications and their fixed deadlines. Maps without these terrain
types keep the existing optimized land/water path. Binary/text saves, continuation, scene
isolation and eager/resumed fields are covered by `PrototypeTerrain/*`; terrain painting and
rules are covered by `TerrainResources/*`. Platform checksum equivalence and human balance
review remain separate requirements. `PrototypeTerrainRender/*` compares cached-path
rendering with direct layers in software and OpenGL. Text saves retain historical scoped
statistics keys, and the parser distinguishes their double colons from section inheritance;
`TextStream/*` covers both forms.
