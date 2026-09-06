// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "Map.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"
#include "Utilities.h"
#include "MapInternal.h"
#include "PathfindPolicy.h"
#include "PathfindStats.h"

#include <algorithm>
#include <vector>

// Weighted distance fields (alternative pathfinder, see PathfindPolicy.h).
//
// A field holds, per cell, the integer cost of the cheapest path from that
// cell to any goal cell. Costs are octile: a cardinal step costs
// WEIGHT_CARDINAL, a diagonal step WEIGHT_DIAGONAL. Entering a water cell
// costs WATER_CARDINAL_COST[swimClass] instead of WEIGHT_CARDINAL (times
// 14/10 diagonally); the class is derived from the unit's own walk/swim
// speed ratio, so a swim-trained slow walker prefers water and a fresh
// swimmer avoids it. Class 0 cannot swim and its seed marks water as an
// obstacle. Fields are built with Dial's bucket-queue Dijkstra: every
// bucket is a FIFO and neighbours are relaxed in tabClose order, so the
// result and every tie-break are fully determined by the input.

namespace
{
	constexpr int WEIGHT_CARDINAL = 10;
	constexpr int WEIGHT_DIAGONAL = 14;
	// Cost of entering a water cell (cardinal) per swim class; 0 = cannot swim.
	constexpr int WATER_CARDINAL_COST[Map::SWIM_CLASS_COUNT] = { 0, 5, 7, 10, 13, 20, 30 };
	constexpr int MAX_EDGE = 30 * WEIGHT_DIAGONAL / WEIGHT_CARDINAL; // 42
	constexpr int BUCKETS = MAX_EDGE + 1;
	// Costs above this are treated as unreachable so cost + MAX_EDGE never wraps.
	constexpr int COST_LIMIT = Map::COST_INFINITY - MAX_EDGE - 1;
	constexpr Uint32 DIRTY_REBUILD_TICKS = 25;

	std::vector<int> buckets[BUCKETS];

	struct SeedCell
	{
		Uint16 cost;
		int index;
		bool operator<(const SeedCell &o) const { return cost != o.cost ? cost < o.cost : index < o.index; }
	};
	std::vector<SeedCell> seedCells;
}

static_assert(Building::PATHFIND_SWIM_CLASS_COUNT == Map::SWIM_CLASS_COUNT);

int Map::swimClass(int walkSpeed, int swimSpeed)
{
	if (swimSpeed <= 0)
		return 0;
	if (walkSpeed <= 0)
		return SWIM_CLASS_COUNT - 1;
	int ratio10 = (10 * walkSpeed + swimSpeed / 2) / swimSpeed; // 10 * walk / swim, rounded
	// Bucket to the closest WATER_CARDINAL_COST entry: 5, 7, 10, 13, 20, 30.
	if (ratio10 <= 6) return 1;
	if (ratio10 <= 8) return 2;
	if (ratio10 <= 11) return 3;
	if (ratio10 <= 16) return 4;
	if (ratio10 <= 24) return 5;
	return 6;
}

int Map::weightedStepCost(int dx, int dy, size_t targetIndex, int swimClass) const
{
	int base = WEIGHT_CARDINAL;
	if (swimClass > 0 && isWater((unsigned)targetIndex))
		base = WATER_CARDINAL_COST[swimClass];
	if (dx != 0 && dy != 0)
		base = base * WEIGHT_DIAGONAL / WEIGHT_CARDINAL;
	return base;
}

// Core Dijkstra. seedCells must be sorted; obstacle cells are those with
// obstacles[i] == GRADIENT_FORBIDDEN.
void Map::runWeightedDijkstra(const Uint8 *obstacles, Uint16 *cost, int swimClass)
{
	PathfindStats::Scope pfScope(PathfindStats::get().weightedField);
	for (int b = 0; b < BUCKETS; b++)
		buckets[b].clear();
	for (size_t i = 0; i < size; i++)
		cost[i] = COST_INFINITY;

	size_t pending = 0;
	size_t nextSeed = 0;
	int cur = 0;
	while ((pending > 0 || nextSeed < seedCells.size()) && cur <= COST_LIMIT)
	{
		// Inject seeds whose cost has been reached. Seeds are sorted, so any
		// seed with cost < cur was already injected or is dominated.
		while (nextSeed < seedCells.size() && seedCells[nextSeed].cost <= cur)
		{
			const SeedCell &s = seedCells[nextSeed++];
			if (s.cost < cost[s.index])
			{
				cost[s.index] = s.cost;
				buckets[s.cost % BUCKETS].push_back(s.index);
				pending++;
			}
		}
		if (pending == 0)
		{
			// Jump to the next seed instead of spinning through empty buckets.
			cur = seedCells[nextSeed].cost;
			continue;
		}
		std::vector<int> &bucket = buckets[cur % BUCKETS];
		// Relaxations may append to other buckets but never to this one
		// (every edge costs at least 5 > 0), so iterating by index is safe.
		for (size_t bi = 0; bi < bucket.size(); bi++)
		{
			int i = bucket[bi];
			pending--;
			if (cost[i] != cur)
				continue; // stale entry, a cheaper path was found later
			size_t x = i & wMask;
			size_t y = i >> wDec;
			for (int d = 0; d < 8; d++)
			{
				int dx = tabClose[d][0];
				int dy = tabClose[d][1];
				size_t nx = (x + dx) & wMask;
				size_t ny = (y + dy) & hMask;
				size_t n = (ny << wDec) | nx;
				if (obstacles[n] == GRADIENT_FORBIDDEN)
					continue;
				// The step from n to i enters i; charge i's terrain.
				int nc = cur + weightedStepCost(dx, dy, (size_t)i, swimClass);
				if (nc < cost[n])
				{
					cost[n] = (Uint16)nc;
					buckets[nc % BUCKETS].push_back((int)n);
					pending++;
				}
			}
		}
		bucket.clear();
		cur++;
	}
}

void Map::buildWeightedField(const Uint8 *seed, Uint16 *cost, int swimClass)
{
	seedCells.clear();
	for (size_t i = 0; i < size; i++)
		if (seed[i] == GRADIENT_AT_GOAL)
			seedCells.push_back(SeedCell{0, (int)i});
	runWeightedDijkstra(seed, cost, swimClass);
}

void Map::writeGradientFromCost(const Uint16 *cost, Uint8 *gradient) const
{
	for (size_t i = 0; i < size; i++)
	{
		if (gradient[i] == GRADIENT_FORBIDDEN)
			continue;
		if (cost[i] == COST_INFINITY)
			gradient[i] = GRADIENT_UNREACHABLE;
		else
		{
			int tiles = (cost[i] + WEIGHT_CARDINAL / 2) / WEIGHT_CARDINAL;
			if (tiles > GRADIENT_AT_GOAL - 2)
				tiles = GRADIENT_AT_GOAL - 2;
			gradient[i] = (Uint8)(GRADIENT_AT_GOAL - tiles);
		}
	}
}

bool Map::directionByCost(Uint32 teamMask, int swimClass, int x, int y, const Uint16 *cost, int *dx, int *dy, bool strict) const
{
	PathfindStats::get().directionByCostCalls++;
	bool canSwim = swimClass > 0;
	size_t here = coordToIndex(x, y);
	int current = cost[here];
	if (current == COST_INFINITY)
		return false;
	if (current == 0)
	{
		*dx = 0;
		*dy = 0;
		return true;
	}
	int best = COST_INFINITY;
	int bestD = -1;
	int sidesteps[8];
	int sidestepCount = 0;
	for (int d = 0; d < 8; d++)
	{
		int ddx = tabClose[d][0];
		int ddy = tabClose[d][1];
		size_t n = coordToIndex(x + ddx, y + ddy);
		int c = cost[n];
		if (c == COST_INFINITY)
			continue;
		if (!isFreeForGroundUnit(x + ddx, y + ddy, canSwim, teamMask))
			continue;
		if (c < current)
		{
			// Total cost of going through n: the step there plus n's cost-to-go.
			int total = c + weightedStepCost(ddx, ddy, n, swimClass);
			if (total < best)
			{
				best = total;
				bestD = d;
			}
		}
		else if (c == current)
			sidesteps[sidestepCount++] = d;
	}
	if (bestD >= 0)
	{
		*dx = tabClose[bestD][0];
		*dy = tabClose[bestD][1];
		return true;
	}
	if (strict || sidestepCount == 0)
		return false;
	// Blocked: sidestep to a random neighbour that is no farther from the
	// goal. Random so that two units blocking each other do not mirror each
	// other forever; syncRand keeps it deterministic.
	int pick = sidesteps[syncRand() % sidestepCount];
	*dx = tabClose[pick][0];
	*dy = tabClose[pick][1];
	return true;
}

// Resource fields: one per swim class in use, built from the canSwim seed.
void Map::buildResourceClassFields(int teamNumber, Uint8 resourceType, bool canSwim, Uint8 *gradient)
{
	Uint32 classes = canSwim ? (activeSwimClasses[teamNumber] & ~1u) : 1u;
	if (canSwim && classes == 0)
		classes = 1u << DEFAULT_SWIM_CLASS;
	int lowest = -1;
	for (int c = 0; c < SWIM_CLASS_COUNT; c++)
	{
		if (!((classes >> c) & 1u))
			continue;
		Uint16 *&cost = resourcesCost[teamNumber][resourceType][c];
		if (cost == NULL)
			cost = new Uint16[size];
		buildWeightedField(gradient, cost, c);
		resourcesCostVersion[teamNumber][resourceType][c]++;
		if (lowest < 0)
			lowest = c;
	}
	writeGradientFromCost(resourcesCost[teamNumber][resourceType][lowest], gradient);
}

// Building fields: one per swim class in use, built from the canSwim seed.
void Map::buildBuildingClassFields(Building *building, bool canSwim, Uint8 *gradient)
{
	int teamNumber = building->owner->teamNumber;
	Uint32 classes = canSwim ? (activeSwimClasses[teamNumber] & ~1u) : 1u;
	if (canSwim && classes == 0)
		classes = 1u << DEFAULT_SWIM_CLASS;
	int lowest = -1;
	for (int c = 0; c < SWIM_CLASS_COUNT; c++)
	{
		if (!((classes >> c) & 1u))
			continue;
		Uint16 *&cost = building->globalCost[c];
		if (cost == NULL)
			cost = new Uint16[size];
		buildWeightedField(gradient, cost, c);
		building->globalCostVersion[c]++;
		building->weightedFieldDirty[c] = false;
		building->lastWeightedUpdateStep[c] = game->stepCounter;
		if (lowest < 0)
			lowest = c;
	}
	writeGradientFromCost(building->globalCost[lowest], gradient);
}

// Make sure the building has a fresh field for this class; returns NULL if
// the building is unreachable for that class.
const Uint16 *Map::ensureBuildingField(Building *building, int swimClass)
{
	int teamNumber = building->owner->teamNumber;
	activeSwimClasses[teamNumber] |= 1u << swimClass;
	bool canSwim = swimClass > 0;
	Uint32 now = game->stepCounter;
	bool rebuild = false;
	if (building->globalGradient[canSwim] == NULL)
	{
		building->globalGradient[canSwim] = new Uint8[size];
		rebuild = true;
	}
	else if (building->globalCost[swimClass] == NULL)
		rebuild = true;
	else if (building->weightedFieldDirty[swimClass] && building->lastWeightedUpdateStep[swimClass] + DIRTY_REBUILD_TICKS <= now)
		rebuild = true;
	if (rebuild)
		updateGlobalGradient(building, canSwim);
	if (building->locked[canSwim])
		return NULL;
	return building->globalCost[swimClass];
}

// Composed field for (building, resource, class): seeded at every resource
// tile with the cost of carrying from there to the building, so descending
// it from the unit minimises d(unit, tile) + d(tile, building). Returns NULL
// when the plain resource field has not been built yet.
const Uint16 *Map::composedField(Building *building, int resourceType, int swimClass)
{
	int teamNumber = building->owner->teamNumber;
	const Uint16 *bcost = ensureBuildingField(building, swimClass);
	if (bcost == NULL)
		return NULL;
	const Uint16 *rcost = resourcesCost[teamNumber][resourceType][swimClass];
	if (rcost == NULL)
		return NULL;
	Uint32 rver = resourcesCostVersion[teamNumber][resourceType][swimClass];
	Uint32 bver = building->globalCostVersion[swimClass];
	Uint16 *&composed = building->composedCost[resourceType][swimClass];
	if (composed != NULL
		&& building->composedResVersion[resourceType][swimClass] == rver
		&& building->composedBldVersion[resourceType][swimClass] == bver)
		return composed;
	if (composed == NULL)
		composed = new Uint16[size];
	bool canSwim = swimClass > 0;
	const Uint8 *seed = resourcesGradient[teamNumber][resourceType][canSwim];
	seedCells.clear();
	for (size_t i = 0; i < size; i++)
	{
		if (seed[i] != GRADIENT_AT_GOAL)
			continue;
		// The unit harvests from a neighbouring free cell and carries from there.
		size_t x = i & wMask;
		size_t y = i >> wDec;
		int best = COST_INFINITY;
		for (int d = 0; d < 8; d++)
		{
			size_t n = (((y + tabClose[d][1]) & hMask) << wDec) | ((x + tabClose[d][0]) & wMask);
			if (seed[n] == GRADIENT_FORBIDDEN || seed[n] == GRADIENT_AT_GOAL)
				continue;
			if (bcost[n] < best)
				best = bcost[n];
		}
		if (best < COST_LIMIT)
			seedCells.push_back(SeedCell{(Uint16)best, (int)i});
	}
	std::sort(seedCells.begin(), seedCells.end());
	runWeightedDijkstra(seed, composed, swimClass);
	building->composedResVersion[resourceType][swimClass] = rver;
	building->composedBldVersion[resourceType][swimClass] = bver;
	PathfindStats::get().composedFieldBuilds++;
	return composed;
}

// Round-trip distance in tiles (unit -> best resource tile -> building) for
// hiring decisions. False when no composed field or unreachable.
bool Map::roundTripDistance(Building *building, int resourceType, int swimClass, int x, int y, int *dist)
{
	const Uint16 *composed = composedField(building, resourceType, swimClass);
	if (composed == NULL)
		return false;
	Uint16 c = composed[coordToIndex(x, y)];
	if (c == COST_INFINITY)
		return false;
	*dist = (c + WEIGHT_CARDINAL / 2) / WEIGHT_CARDINAL;
	return true;
}
