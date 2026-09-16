// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real terrain regeneration and resource clearing without a window.
#include "Building.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Race.h"
#include "Unit.h"

#include <cassert>
#include <cstdio>
#include <vector>

GlobalContainer* globalContainer = nullptr;

int main()
{
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	const int positions[][2] = {{8, 8}, {0, 0}, {15, 0}, {0, 15}, {15, 15}};
	int strokes = 0;
	for (int type = 0; type < MAX_RESOURCES; ++type)
		for (TerrainType paint : {GRASS, SAND, WATER, ICE, COBBLESTONE})
			for (const auto& position : positions)
			{
				Map map;
				map.setSize(4, 4, static_cast<TerrainType>(globals.resourcesTypes.get(type)->terrain));
				for (int y = 0; y < 16; ++y)
					for (int x = 0; x < 16; ++x)
					{
						auto& resource = map.getResource(x, y);
						resource.type = type;
						resource.amount = 3;
					}

				// An adjacent second stroke also checks overlapping brush footprints.
				for (int offset : {0, 1})
				{
					const int px = position[0] + offset, py = position[1];
					std::vector<Resource> before;
					for (int y = 0; y < 16; ++y)
						for (int x = 0; x < 16; ++x)
							before.push_back(map.getResource(x, y));

					// The map operations used by MapEdit::handleTerrainClick.
					map.setUMatPos(px, py, paint, 1);
					map.removeUnallowedResources(px - 2, py - 2, 4, 4);
					// No corner the stroke leaves beside the painted one may be forbidden to touch it.
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const TerrainType n = map.getUMTerrain(px + dx, py + dy);
							auto pair = [&](TerrainType a, TerrainType b) { return (paint == a && n == b) || (paint == b && n == a); };
							assert(!pair(GRASS, WATER) && !pair(COBBLESTONE, WATER));
						}
					if (paint == GRASS)
						for (int y = py - 1; y <= py; ++y)
							for (int x = px - 1; x <= px; ++x)
								map.getResource(x, y).clear();

					// Independent whole-map oracle: retain every compatible resource,
					// except the four tiles explicitly cleared by the grass brush.
					for (int y = 0; y < 16; ++y)
						for (int x = 0; x < 16; ++x)
						{
							Resource expected = before[y * 16 + x];
							const bool bareGrass = paint == GRASS &&
								(x == (px & 15) || x == ((px - 1) & 15)) &&
								(y == (py & 15) || y == ((py - 1) & 15));
							if (bareGrass || (expected.type != NO_RES_TYPE &&
								map.getTerrainType(x, y) != globals.resourcesTypes.get(expected.type)->terrain))
								expected.clear();
							assert(map.getResource(x, y).getUint32() == expected.getUint32());
						}
					++strokes;
				}
			}
	std::printf("Terrain resource regressions passed: %d strokes, all 8 resources, 5 terrains, interior and four wrapped corners\n", strokes);

	// Buildings and units. After a stroke a building stands iff its whole footprint is
	// still grass or cobblestone, a walker iff its tile is not water, an explorer always; the grass
	// brush also clears the four tiles the cell touches. The oracle knows nothing about
	// the stroke position, so it holds on every side of an entity alike.
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	const int swarm = globals.buildingsTypes.getTypeNum("swarm", 0, false);
	const int swarmW = globals.buildingsTypes.get(swarm)->width;
	const int swarmH = globals.buildingsTypes.get(swarm)->height;
	constexpr int swarmX = 8, swarmY = 8;

	struct World
	{
		GameGUI gui;
		Game& game = gui.game;
		int swarmW, swarmH;
		int swarmX, swarmY, workerX, workerY, explorerX, explorerY;
		Uint16 swarmGid, workerGid, explorerGid;
		World(int swarm, int w, int h, int offsetX = 0, int offsetY = 0) : swarmW(w), swarmH(h),
            swarmX((8 + offsetX) & 15), swarmY((8 + offsetY) & 15),
            workerX((6 + offsetX) & 15), workerY((9 + offsetY) & 15),
            explorerX((11 + offsetX) & 15), explorerY((9 + offsetY) & 15)
		{
			game.map.setSize(4, 4, GRASS);
			game.map.setGame(&game);
			game.addTeam();
			Building* building = game.addBuilding(swarmX, swarmY, swarm, 0);
			Unit* worker = game.addUnit(workerX, workerY, 0, WORKER, 0, 0, 0, 0);
			Unit* explorer = game.addUnit(explorerX, explorerY, 0, EXPLORER, 0, 0, 0, 0);
			assert(building && worker && explorer && !worker->performance[SWIM]);
			swarmGid = building->gid;
			workerGid = worker->gid;
			explorerGid = explorer->gid;
		}
		bool alive(Uint16 gid, bool building) const
		{
			if (building)
				return game.teams[0]->myBuildings[Building::GIDtoID(gid)] != nullptr;
			return game.teams[0]->myUnits[Unit::GIDtoID(gid)] != nullptr;
		}
		// The map operations used by MapEdit::handleTerrainClick.
		void stroke(int x, int y, TerrainType paint)
		{
			game.map.setUMatPos(x, y, paint, 1);
			game.map.removeUnallowedResources(x - 2, y - 2, 4, 4);
			game.removeUnallowedUnitsAndBuildings(x - 2, y - 2, 4, 4);
			if (paint == GRASS)
				game.removeUnitAndBuildingAndFlags(x, y, 2, Game::DEL_BUILDING | Game::DEL_UNIT);
		}
		void check(int px, int py, TerrainType paint) const
		{
			auto touched = [&](int x, int y)
			{
				return paint == GRASS && (((x - px) & 15) == 0 || ((x - px) & 15) == 15)
                    && (((y - py) & 15) == 0 || ((y - py) & 15) == 15);
			};
			bool swarmStands = true;
			for (int y = swarmY; y < swarmY + swarmH; ++y)
				for (int x = swarmX; x < swarmX + swarmW; ++x)
					if ((!game.map.isGrass(x & 15, y & 15) && !game.map.isCobblestone(x & 15, y & 15)) || touched(x, y))
						swarmStands = false;
			const bool workerStands = !game.map.isWater(workerX, workerY) && !touched(workerX, workerY);
			const bool explorerStands = !touched(explorerX, explorerY);
			assert(alive(swarmGid, true) == swarmStands);
			assert((game.map.getBuilding(swarmX, swarmY) != NOGBID) == swarmStands);
			assert(alive(workerGid, false) == workerStands);
			assert((game.map.getGroundUnit(workerX, workerY) != NOGUID) == workerStands);
			assert(alive(explorerGid, false) == explorerStands);
			assert((game.map.getAirUnit(explorerX, explorerY) != NOGUID) == explorerStands);
		}
	};

	int entityStrokes = 0;
    const int offsets[][2] = {{0, 0}, {-8, -8}, {7, -8}, {-8, 7}, {7, 7}};
    for (const auto& offset : offsets)
        for (TerrainType paint : {GRASS, SAND, WATER, ICE, COBBLESTONE})
            for (int py = 4; py <= 13; ++py)
                for (int px = 4; px <= 13; ++px)
                {
                    World world(swarm, swarmW, swarmH, offset[0], offset[1]);
                    const int x = (px + offset[0]) & 15;
                    const int y = (py + offset[1]) & 15;
                    world.stroke(x, y, paint);
                    world.check(x, y, paint);
                    ++entityStrokes;
                }

	// A cell touches the tiles c-1..c; the cell 2*swarmX+swarmW-c touches their mirror
	// image across the swarm, the same distance east as c is west, and must agree.
	for (TerrainType paint : {GRASS, SAND, WATER, ICE, COBBLESTONE})
		for (int c = 4; c <= swarmX; ++c)
		{
			World west(swarm, swarmW, swarmH), east(swarm, swarmW, swarmH);
			west.stroke(c, swarmY, paint);
			east.stroke(2 * swarmX + swarmW - c, swarmY, paint);
			assert(west.alive(west.swarmGid, true) == east.alive(east.swarmGid, true));
		}

	// Co-located air units survive flooding, while only swimming ground units remain.
    for (bool swimming : {false, true})
        for (const auto& offset : offsets)
        {
            World world(swarm, swarmW, swarmH, offset[0], offset[1]);
            Unit* worker = world.game.getUnit(world.workerGid);
            worker->performance[SWIM] = swimming ? worker->race->getUnitType(WORKER, 1)->performance[SWIM] : 0;
            Unit* explorer = world.game.getUnit(world.explorerGid);
            world.game.map.setAirUnit(explorer->posX, explorer->posY, NOGUID);
            explorer->posX = world.workerX;
            explorer->posY = world.workerY;
            world.game.map.setAirUnit(explorer->posX, explorer->posY, explorer->gid);
            for (int y = world.workerY; y <= world.workerY + 1; ++y)
                for (int x = world.workerX; x <= world.workerX + 1; ++x)
                    world.stroke(x & 15, y & 15, WATER);
            assert(world.game.map.isWater(world.workerX, world.workerY));
            assert(world.alive(world.workerGid, false) == swimming);
            assert(world.game.map.getGroundUnit(world.workerX, world.workerY) == (swimming ? world.workerGid : NOGUID));
            assert(world.alive(world.explorerGid, false));
            assert(world.game.map.getAirUnit(world.workerX, world.workerY) == world.explorerGid);
            ++entityStrokes;
        }

    // A single deletion flag addresses exactly one occupancy layer.
    for (unsigned flag : {unsigned(Game::DEL_GROUND_UNIT), unsigned(Game::DEL_AIR_UNIT)})
    {
        World world(swarm, swarmW, swarmH);
        Unit* explorer = world.game.getUnit(world.explorerGid);
        world.game.map.setAirUnit(explorer->posX, explorer->posY, NOGUID);
        explorer->posX = world.workerX;
        explorer->posY = world.workerY;
        world.game.map.setAirUnit(explorer->posX, explorer->posY, explorer->gid);
        world.game.removeUnitAndBuildingAndFlags(world.workerX, world.workerY, 1, flag);
        assert(world.alive(world.workerGid, false) == (flag == Game::DEL_AIR_UNIT));
        assert(world.alive(world.explorerGid, false) == (flag == Game::DEL_GROUND_UNIT));
        assert(world.game.map.getGroundUnit(world.workerX, world.workerY) == (flag == Game::DEL_AIR_UNIT ? world.workerGid : NOGUID));
        assert(world.game.map.getAirUnit(world.workerX, world.workerY) == (flag == Game::DEL_GROUND_UNIT ? world.explorerGid : NOGUID));
        assert(world.alive(world.swarmGid, true));
    }
    std::printf("Terrain entity regressions passed: %d strokes, 5 terrains, interior and four wrapped corners, swimmers and separate occupancy layers\n", entityStrokes);

	// Prototype terrain rules on a grass map with a strip of ice (column 4) and a patch of
	// cobblestone (columns 8-11), painted through the editor's map operation.
	{
		World world(swarm, swarmW, swarmH);
		Map& map = world.game.map;
		assert(!map.hasCobblestone());
		const int landDiagonal = map.stepCost(1, 1, map.coordToIndex(2, 2), 0);
		const int land = map.stepCost(1, 0, map.coordToIndex(2, 2), 0);
		assert(map.minStepCost(0) == land);
		for (int y = 0; y < 16; ++y)
			map.setUMatPos(4, y, ICE, 1);
		for (int y = 0; y < 16; ++y)
			for (int x = 8; x <= 12; ++x)
				map.setUMatPos(x, y, COBBLESTONE, 1);
		assert(map.isIce(4, 3) && map.isIce(3, 3) && !map.isIce(5, 3) && !map.isIce(2, 3));
		assert(map.isCobblestone(8, 3) && map.isCobblestone(11, 3) && !map.isCobblestone(7, 3) && !map.isCobblestone(12, 3));
		assert(map.hasCobblestone());
		// Neither is grass, sand or water, so no resource belongs on either.
		for (int type = 0; type < MAX_RESOURCES; ++type)
			assert(!map.isResourceAllowed(4, 3, type) && !map.isResourceAllowed(9, 3, type));
		// Buildings stand on cobblestone and grass, not on ice.
		assert(map.isFreeForBuilding(9, 2) && map.isFreeForBuilding(1, 2) && !map.isFreeForBuilding(4, 2));
		// Steps into ice cost three land steps, into cobblestone half of one, diagonals alike.
		assert(map.stepCost(1, 0, map.coordToIndex(4, 3), 0) == 3 * land);
		assert(map.stepCost(1, 0, map.coordToIndex(9, 3), 0) == land / 2);
		assert(map.stepCost(1, 1, map.coordToIndex(9, 3), 3) == landDiagonal / 2);
		assert(map.minStepCost(0) == land / 2);

		// Walking speed doubles on cobblestone and halves on ice; the worker's other actions do not change.
		Unit* worker = world.game.getUnit(world.workerGid);
		const int walk = worker->performance[WALK];
		auto moveTo = [&](int x, int y)
		{
			map.setGroundUnit(worker->posX, worker->posY, NOGUID);
			worker->posX = x;
			worker->posY = y;
			map.setGroundUnit(x, y, worker->gid);
		};
		worker->action = WALK;
		moveTo(9, 5);
		assert(worker->terrainSpeed(walk) == 2 * walk);
		moveTo(4, 5);
		assert(worker->terrainSpeed(walk) == walk / 2);
		moveTo(1, 5);
		assert(worker->terrainSpeed(walk) == walk);
		worker->action = HARVEST;
		moveTo(4, 5);
		assert(worker->terrainSpeed(walk) == walk);

		// Ice takes 1 HP every 32 ticks from ground units; none from units elsewhere or explorers.
		Unit* explorer = world.game.getUnit(world.explorerGid);
		map.setAirUnit(explorer->posX, explorer->posY, NOGUID);
		explorer->posX = 4;
		explorer->posY = 6;
		map.setAirUnit(4, 6, explorer->gid);
		const int workerHP = worker->hp, explorerHP = explorer->hp;
		for (Uint32 tick = 1; tick <= 320; ++tick)
		{
			world.game.stepCounter = tick;
			worker->takeTerrainDamage();
			explorer->takeTerrainDamage();
		}
		assert(worker->hp == workerHP - 10 && explorer->hp == explorerHP);
		moveTo(1, 5);
		for (Uint32 tick = 321; tick <= 640; ++tick)
		{
			world.game.stepCounter = tick;
			worker->takeTerrainDamage();
		}
		assert(worker->hp == workerHP - 10);
	}
	std::printf("Terrain rule regressions passed: ice and cobblestone placement, step costs, speed and ice damage\n");
    return 0;
}
