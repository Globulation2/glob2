// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <vector>

namespace
{
	// Scratch for computeWarriorCrowding and the guard seeding; serial use only,
	// like the gradient buckets.
	std::vector<Uint16> crowdCells;
	std::vector<Uint16> paintCells;
	std::vector<Uint16> crowdRows;
	std::vector<int> crowdColumnSums;
	std::vector<size_t> crowdPositions;
	std::vector<size_t> guardSeeds;
}



// Forbidden / Guard area / Clear area gradients

Uint16 *Map::getForbiddenGradient(int teamNumber, int swimClass)
{
	Uint16 *&gradient = forbiddenGradient[teamNumber][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateForbiddenGradient(teamNumber, swimClass);
	}
	return gradient;
}

Uint16 *Map::getGuardAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *&gradient = guardAreasGradient[teamNumber][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateGuardAreasGradient(teamNumber, swimClass);
	}
	return gradient;
}

Uint16 *Map::getClearAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *&gradient = clearAreasGradient[teamNumber][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateClearAreasGradient(teamNumber, swimClass);
	}
	return gradient;
}

void Map::updateForbiddenGradient(int teamNumber, int swimClass)
{
	Uint16 *gradient = forbiddenGradient[teamNumber][swimClass];
	assert(gradient);
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	bool canSwim = swimClass > 0;

	// Seed: free cells are goals, forbidden interiors are placeholders (promoted
	// to GRADIENT_FORBIDDEN_BORDER in the second pass if they border a free cell),
	// all other blockers (resources, buildings, water, immobileUnits) are obstacles.
	for (size_t i=0; i<size; i++)
	{
		const Tile& c=tiles[i];
		if (c.resource.type!=NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building!=NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.forbidden&teamMask)
			gradient[i] = GRADIENT_UNREACHABLE;
		else
			gradient[i] = GRADIENT_AT_GOAL;
	}

	// Forbidden cells bordering free cells become GRADIENT_FORBIDDEN_BORDER sources
	// so the gradient fades outward into the forbidden zone.
	for (size_t i=0; i<size; i++)
	{
		if (gradient[i] != GRADIENT_UNREACHABLE)
			continue;
		size_t y = i >> wDec;
		size_t x = i & wMask;
		for (int d=0; d<8; d++)
		{
			size_t n = (((y + tabClose[d][1]) & hMask) << wDec) | ((x + tabClose[d][0]) & wMask);
			if (gradient[n] == GRADIENT_AT_GOAL)
			{
				gradient[i] = GRADIENT_FORBIDDEN_BORDER;
				break;
			}
		}
	}

	propagateGradient(gradient, swimClass);
}

void Map::updateForbiddenGradient(int teamNumber)
{
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (forbiddenGradient[teamNumber][c])
			updateForbiddenGradient(teamNumber, c);
}

void Map::updateForbiddenGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateForbiddenGradient(i);
}


// Warrior crowding for the guard gradient. Every tile ends up holding the
// number of the team's warriors within GUARD_CROWD_RADIUS tiles of it: warrior
// positions are splatted onto a map-sized grid and box-summed. Warriors inside
// buildings are not counted; they are not guarding anything.
bool Map::computeWarriorCrowding(int teamNumber, Uint16 *out) const
{
	// Gather positions first: a team without warriors costs one pass over its
	// unit slots and nothing over the map.
	crowdPositions.clear();
	const Team *team = game->teams[teamNumber];
	for (int i = 0; i < Unit::MAX_COUNT; i++)
	{
		const Unit *u = team->myUnits[i];
		if (!u || u->isDead || u->typeNum != WARRIOR || u->displacement == Unit::DIS_INSIDE)
			continue;
		crowdPositions.push_back(coordToIndex(u->posX, u->posY));
	}
	if (crowdPositions.empty())
		return false;
	std::fill(out, out + size, 0);
	for (size_t i : crowdPositions)
		out[i]++;
	boxSumInPlace(out);
	return true;
}

// Box sum over a (2r+1)-square window, in place. A box filter is separable: sum
// each row over the window, then sum those row sums down each column, and every
// cell holds the sum of the whole square. Each pass is a sliding window that
// adds the cell entering and subtracts the one leaving, so the cost is a few
// operations per cell whatever the radius. Both passes walk memory in row
// order; the column pass carries one running sum per column across the rows
// rather than striding down each column. The map is a torus and its sides are
// powers of two, so the window wraps by masking the index, with no edge cases.
void Map::boxSumInPlace(Uint16 *grid) const
{
	// A window wider than the map would count a cell twice.
	const int r = std::min<int>(GUARD_CROWD_RADIUS, std::min((int)w - 1, (int)h - 1) / 2);
	crowdRows.assign(size, 0);
	// Rows: window [x-r, x+r] slides right; entering x+r+1, leaving x-r.
	for (int y = 0; y < (int)h; y++)
	{
		const size_t row = (size_t)y << wDec;
		int sum = 0;
		for (int dx = -r; dx <= r; dx++)
			sum += grid[row | (size_t)(dx & (int)wMask)];
		for (int x = 0; x < (int)w; x++)
		{
			crowdRows[row | (size_t)x] = (Uint16)sum;
			sum += grid[row | (size_t)((x + r + 1) & (int)wMask)] - grid[row | (size_t)((x - r) & (int)wMask)];
		}
	}
	// Columns, the same way over the row sums: crowdColumnSums[x] is the running
	// window sum of column x, advanced one whole row at a time so the sweep
	// stays row-major and the inner loops vectorise.
	crowdColumnSums.assign(w, 0);
	for (int dy = -r; dy <= r; dy++)
	{
		const Uint16 *in = &crowdRows[(size_t)(dy & (int)hMask) << wDec];
		for (int x = 0; x < (int)w; x++)
			crowdColumnSums[x] += in[x];
	}
	for (int y = 0; y < (int)h; y++)
	{
		Uint16 *o = grid + ((size_t)y << wDec);
		const Uint16 *entering = &crowdRows[(size_t)((y + r + 1) & (int)hMask) << wDec];
		const Uint16 *leaving = &crowdRows[(size_t)((y - r) & (int)hMask) << wDec];
		for (int x = 0; x < (int)w; x++)
		{
			o[x] = (Uint16)crowdColumnSums[x];
			crowdColumnSums[x] += entering[x] - leaving[x];
		}
	}
}

// The guard gradient: the field free warriors climb to reach a guard area.
// Obstacles are forbidden tiles, resources, allied buildings, water for
// non-swimmers and immobile units; every other tile is unreachable until the
// propagation reaches it from a painted tile.
//
// Painted tiles are the seeds. They are not seeded at cost zero: each carries a
// crowding cost, warriors nearby per painted tile nearby (MapInternal.h), so
// Dijkstra gives every tile the best of "distance to an area plus that area's
// crowding". Two areas then settle where their crowding difference matches the
// walking distance between them, a larger painted area costs less per warrior
// and so takes more of them, and an area that is over-full has another area's
// field running over its painted tiles, which is what lets its warriors find
// the way out (Map::pathfindArea).
void Map::updateGuardAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *gradient = guardAreasGradient[teamNumber][swimClass];
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	guardSeeds.clear();
	for (size_t i=0; i<size; i++)
	{
		const Tile& c=tiles[i];
		if (c.forbidden & teamMask)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type != NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID && (1<<Building::GIDtoTeam(c.building)) & (game->teams[teamNumber]->allies))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.guardArea & teamMask)
		{
			gradient[i] = GRADIENT_AT_GOAL;
			guardSeeds.push_back(i);
		}
		else
			gradient[i] = GRADIENT_UNREACHABLE;
	}

	// Crowding costs. Both box sums are skipped for a team that painted nothing
	// (the common case: most teams never use guard areas) and for a team with
	// no warriors, whose painted tiles stay at cost zero.
	if (!guardSeeds.empty())
	{
		crowdCells.resize(size);
		if (computeWarriorCrowding(teamNumber, crowdCells.data()))
		{
			paintCells.assign(size, 0);
			for (size_t i : guardSeeds)
				paintCells[i] = 1;
			boxSumInPlace(paintCells.data());
			for (size_t i : guardSeeds)
			{
				// Warriors per painted tile in the window, scaled so that a
				// 25-tile area pays the full per-warrior cost. paintCells[i] is
				// at least 1: the seed itself is painted.
				const int cost = (GUARD_CROWD_COST_PER_WARRIOR * crowdCells[i] * GUARD_CROWD_REFERENCE_AREA) / std::max<int>(1, paintCells[i]);
				gradient[i] = (Uint16)(GRADIENT_AT_GOAL - std::min(GUARD_CROWD_COST_MAX, cost));
			}
		}
	}

	propagateGradient(gradient, swimClass);
}

void Map::updateGuardAreasGradient(int teamNumber)
{
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (guardAreasGradient[teamNumber][c])
			updateGuardAreasGradient(teamNumber, c);
}

void Map::updateGuardAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateGuardAreasGradient(i);
}


void Map::updateClearAreasGradient(int teamNumber, int swimClass)
{
	Uint16 *gradient = clearAreasGradient[teamNumber][swimClass];
	assert(gradient);
	bool canSwim = swimClass > 0;

	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
	{
		const Tile& c=tiles[i];
		if (c.forbidden & teamMask)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if(c.clearArea & teamMask && c.resource.type != NO_RES_TYPE && globalContainer->resourcesTypes.get(c.resource.type)->clearable)
			gradient[i] = GRADIENT_AT_GOAL;
		else if(immobileUnits[i] != IMMOBILE_UNIT_NONE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type != NO_RES_TYPE)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID)
			gradient[i] = GRADIENT_FORBIDDEN;
		else if (!canSwim && isWater(i))
			gradient[i] = GRADIENT_FORBIDDEN;
		else
			gradient[i] = GRADIENT_UNREACHABLE;
	}

	propagateGradient(gradient, swimClass);
}

void Map::updateClearAreasGradient(int teamNumber)
{
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
		if (clearAreasGradient[teamNumber][c])
			updateClearAreasGradient(teamNumber, c);
}

void Map::updateClearAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateClearAreasGradient(i);
}
