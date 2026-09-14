// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the farm area's pooled harvest against the real Map, without a window.
//
// The rule under test: inside a team's farm area a harvest draws one unit off
// the ripest tile of the connected field the worker can reach, instead of off
// the tile it is standing next to, and an exhausted field yields nothing.
// Outside a farm area the old behaviour must be untouched, phantom grain
// included, since that is what keeps a game with no farm painted identical to
// master.
#include "GlobalContainer.h"
#include "Map.h"
#include "Team.h"

#include <cassert>
#include <cstdio>
#include <vector>

GlobalContainer* globalContainer = nullptr;

namespace
{
	const Uint32 TEAM_MASK = Team::teamNumberToMask(0);

	//! A 16x16 grass map with nothing on it. setSize allocates the real Sector
	//! array, which is fine here: the harness links the client objects.
	void buildMap(Map& map)
	{
		map.setSize(4, 4, GRASS);
	}

	void setWheat(Map& map, int x, int y, int amount)
	{
		Resource& resource = map.getResource(x, y);
		resource.type = WHEAT;
		resource.variety = 0;
		resource.amount = amount;
		resource.animation = 0;
	}

	int wheatAt(Map& map, int x, int y)
	{
		const Resource& resource = map.getResource(x, y);
		return resource.type == WHEAT ? resource.amount : 0;
	}

	void paintFarm(Map& map, int x0, int y0, int x1, int y1)
	{
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++)
				map.addFarmArea(x, y, 0);
	}

	int totalWheat(Map& map)
	{
		int total = 0;
		for (int y = 0; y < map.getH(); y++)
			for (int x = 0; x < map.getW(); x++)
				total += wheatAt(map, x, y);
		return total;
	}

	//! A worker at (x,y) finishing a harvest against the tile at (x+dx,y+dy).
	bool harvest(Map& map, int x, int y, int dx, int dy)
	{
		return map.takeHarvest(x, y, dx, dy, WHEAT, TEAM_MASK);
	}

	void check(bool condition, const char* what)
	{
		if (!condition)
		{
			fprintf(stderr, "FarmAreaHarvestHarness: %s\n", what);
			abort();
		}
	}

	//! Harvesting a farm takes from the ripest tile of the field, not the tile
	//! the worker is touching, so seed tiles at the rim survive the crowd.
	void ripestTileIsTheSource()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 4, 4, 9, 9);
		setWheat(map, 5, 5, 1);   // the rim tile the worker stands next to
		setWheat(map, 6, 5, 3);
		setWheat(map, 7, 5, 5);   // the ripest tile, two steps away
		check(harvest(map, 4, 5, 1, 0), "a stocked field should yield");
		check(wheatAt(map, 7, 5) == 4, "the ripest tile should lose the grain");
		check(wheatAt(map, 5, 5) == 1, "the touched seedling should be untouched");
		check(wheatAt(map, 6, 5) == 3, "an intermediate tile should be untouched");
	}

	//! Connectivity runs through wheat: an empty gap inside the painted area
	//! ends the field, so nothing teleports across it.
	void emptyGapEndsTheField()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 0, 0, 15, 15);
		setWheat(map, 5, 5, 1);
		// (7,5) is empty, so the ripe patch beyond it is a different field.
		setWheat(map, 8, 5, 5);
		setWheat(map, 9, 5, 5);
		check(harvest(map, 4, 5, 1, 0), "the reachable field should yield");
		check(wheatAt(map, 5, 5) == 0, "the only reachable tile should be spent");
		check(wheatAt(map, 8, 5) == 5 && wheatAt(map, 9, 5) == 5,
			"wheat across a gap must not be teleported");
	}

	//! Standing at the bare edge of a painted farm, out of reach of any wheat,
	//! yields nothing however much the farm holds elsewhere.
	void bareEdgeYieldsNothing()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 0, 0, 15, 15);
		setWheat(map, 10, 10, 5);
		check(!harvest(map, 2, 2, 1, 0), "an unreachable field must not yield");
		check(wheatAt(map, 10, 10) == 5, "distant wheat must be untouched");
	}

	//! The phantom grain, farm-scoped: a harvest that completes on a field that
	//! emptied under the animation hands out nothing.
	void exhaustedFieldYieldsNothing()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 0, 0, 15, 15);
		check(!harvest(map, 4, 5, 1, 0), "an empty field must not yield");
		check(totalWheat(map) == 0, "an empty field must stay empty");
	}

	//! The target tile emptying mid-animation is not a failure while another
	//! tile of the field is still within reach.
	void neighbourKeepsTheHarvestAlive()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 0, 0, 15, 15);
		// (5,5) is the tile the worker aimed at and it is now empty; (5,6) is
		// still in reach and connects to the rest of the field.
		setWheat(map, 5, 6, 2);
		setWheat(map, 6, 6, 4);
		check(harvest(map, 4, 5, 1, 0), "a field reachable diagonally should yield");
		check(wheatAt(map, 6, 6) == 3, "the ripest reachable tile should lose the grain");
	}

	//! Off a farm the old path is untouched: the tile the worker touches is the
	//! one that loses a grain, and an empty tile still grants one.
	void offFarmBehaviourIsUnchanged()
	{
		Map map;
		buildMap(map);
		setWheat(map, 5, 5, 1);
		setWheat(map, 7, 5, 5);
		check(harvest(map, 4, 5, 1, 0), "an ordinary harvest yields");
		check(wheatAt(map, 5, 5) == 0, "the touched tile is the source off a farm");
		check(wheatAt(map, 7, 5) == 5, "a ripe tile elsewhere is not pooled off a farm");
		// The phantom: the tile is empty now, and master still grants a grain.
		check(harvest(map, 4, 5, 1, 0), "the off-farm phantom grain is preserved");
	}

	//! Another team's farm area does not change this team's harvest.
	void farmIsPerTeam()
	{
		Map map;
		buildMap(map);
		for (int y = 0; y <= 9; y++)
			for (int x = 0; x <= 9; x++)
				map.addFarmArea(x, y, 1);
		setWheat(map, 5, 5, 1);
		setWheat(map, 6, 5, 5);
		check(harvest(map, 4, 5, 1, 0), "team 0 harvests normally");
		check(wheatAt(map, 5, 5) == 0, "team 1's farm must not pool for team 0");
		check(wheatAt(map, 6, 5) == 5, "team 1's farm must not pool for team 0");
	}

	//! Wood is not granular -- a harvest takes the whole tile -- so a farm area
	//! painted over a forest changes nothing about which tree falls.
	void woodIsNotFarmed()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 0, 0, 15, 15);
		for (int x : {5, 6, 7})
		{
			Resource& resource = map.getResource(x, 5);
			resource.type = WOOD;
			resource.variety = 0;
			resource.amount = (x == 7) ? 5 : 1;
			resource.animation = 0;
		}
		check(map.takeHarvest(4, 5, 1, 0, WOOD, TEAM_MASK), "wood yields as before");
		check(map.getResource(5, 5).type == NO_RES_TYPE, "the touched tree is felled");
		check(map.getResource(7, 5).amount == 5, "a larger tree elsewhere is untouched");
	}

	//! Algae regrow and spread exactly as wheat does, so a farm pools them too.
	void algaeAreFarmed()
	{
		Map map;
		buildMap(map);
		map.setSize(4, 4, WATER);
		paintFarm(map, 0, 0, 15, 15);
		for (int x : {5, 6, 7})
		{
			Resource& resource = map.getResource(x, 5);
			resource.type = ALGA;
			resource.variety = 0;
			resource.amount = (x == 7) ? 4 : 1;
			resource.animation = 0;
		}
		check(map.takeHarvest(4, 5, 1, 0, ALGA, TEAM_MASK), "an alga field yields");
		check(map.getResource(7, 5).amount == 3, "the ripest alga tile is the source");
		check(map.getResource(5, 5).amount == 1, "the touched alga tile survives");
	}

	//! Repeating the same harvest from one spot flattens the field from the top,
	//! and stops when the one tile the worker can touch is finally spent -- the
	//! rest of the field is still there, and the worker has to walk to it.
	void standingStillOnlyDrainsWhatItTouches()
	{
		Map map;
		buildMap(map);
		paintFarm(map, 0, 0, 15, 15);
		setWheat(map, 5, 5, 2);   // the only tile in the worker's 3x3
		setWheat(map, 6, 5, 5);
		setWheat(map, 7, 5, 4);
		const int before = totalWheat(map);
		int harvests = 0;
		while (harvest(map, 4, 5, 1, 0))
		{
			harvests++;
			check(harvests <= 32, "the reachable field must run out");
		}
		check(harvests == 9, "a fixed spot should flatten the field then stop");
		check(totalWheat(map) == before - harvests, "every harvest costs exactly one grain");
		check(wheatAt(map, 5, 5) == 0, "the reachable tile is what finally runs out");
		check(wheatAt(map, 6, 5) == 1 && wheatAt(map, 7, 5) == 1,
			"the rest of the field survives and has to be walked to");
	}
}

int main()
{
	GlobalContainer globals;
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;

	ripestTileIsTheSource();
	emptyGapEndsTheField();
	bareEdgeYieldsNothing();
	exhaustedFieldYieldsNothing();
	neighbourKeepsTheHarvestAlive();
	offFarmBehaviourIsUnchanged();
	farmIsPerTeam();
	woodIsNotFarmed();
	algaeAreFarmed();
	standingStillOnlyDrainsWhatItTouches();

	printf("FarmAreaHarvestHarness: 10 cases passed\n");
	return 0;
}
