// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "MapInternal.h"
#include "Team.h"
#include "Utilities.h"

#include <queue>


// Area pathfinding (forbidden, guard, clear, point-to-point)

bool Map::pathfindForbidden(const Uint16 *optionGradient, int teamNumber, int swimClass, int x, int y, int *dx, int *dy)
{
	const Uint16 *gradient=getForbiddenGradient(teamNumber, swimClass);
	bool canSwim=swimClass>0;

	// Pick the neighbor with the highest (base, option) lexicographically. The base gradient
	// dominates; the option gradient is used as a tiebreaker. Reject results where the chosen
	// base is unreachable (i.e. require base > GRADIENT_UNREACHABLE).
	Uint16 bestBase = 0;
	Uint16 bestOption = 0;
	int maxd = 0;
	for (int di=0; di<8; di++)
	{
		int rx=tabClose[di][0];
		int ry=tabClose[di][1];
		int xg=(x+rx)&wMask;
		int yg=(y+ry)&hMask;
		if (!isFreeForGroundUnitNoForbidden(xg, yg, canSwim))
			continue;
		size_t addr=xg+(yg<<wDec);
		Uint16 base=gradient[addr];
		Uint16 option = (optionGradient!=NULL) ? optionGradient[addr] : 0;
		if (base > bestBase || (base == bestBase && option > bestOption))
		{
			bestBase = base;
			bestOption = option;
			maxd = di;
		}
	}
	if (bestBase > GRADIENT_UNREACHABLE)
	{
		*dx=tabClose[maxd][0];
		*dy=tabClose[maxd][1];
		return true;
	}
	return false;
}

bool Map::pathfindArea(AreaKind kind, int teamNumber, int swimClass, int x, int y, int *dx, int *dy)
{
	const Uint16 *gradient = (kind == AreaKind::Guard)
		? getGuardAreasGradient(teamNumber, swimClass)
		: getClearAreasGradient(teamNumber, swimClass);
	const size_t index = coordToIndex(x, y);
	Uint16 here = gradient[index];
	if (here <= GRADIENT_UNREACHABLE)
		return false; // any existing area is too far away.
	if (kind == AreaKind::Guard)
	{
		const Uint32 teamMask = Team::teamNumberToMask(teamNumber);
		if (tiles[index].guardArea & teamMask)
		{
			// A warrior standing in a guard area. Guard seeds carry a crowding
			// cost, so a painted tile is not necessarily a peak of the field:
			// when this area is over-full, another area's field runs over its
			// tiles and an uphill step leads out. Every warrior here sees the
			// same field, so if all of them followed it at once the whole area
			// would empty and most would walk back. Instead the way out is
			// taken on one action in 2^GUARD_LEAVE_CHANCE_SHIFT: a trickle, and
			// each leaver keeps counting toward this area until it is
			// GUARD_CROWD_RADIUS tiles out, so the next rebuild sees the count
			// fall and stops the trickle before it overshoots (MapInternal.h).
			// A tile at the goal value has nothing higher around it.
			if (here == GRADIENT_AT_GOAL)
				return false;
			if ((syncRand() & ((1 << GUARD_LEAVE_CHANCE_SHIFT) - 1)) == 0)
				return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true);
			// The other actions stay in the area: step onto the highest painted
			// neighbour, which spreads warriors toward the less crowded side of
			// a large area, or report no move so the caller's in-area wander
			// takes over.
			return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true, teamMask);
		}
	}
	else if (here == GRADIENT_AT_GOAL)
		return false; // we already are in a clearing area.

	if (directionByGradient(1<<teamNumber, swimClass, x, y, gradient, dx, dy, true))
		return true;
	if (directionByGradient(1<<teamNumber, swimClass, x, y, gradient, dx, dy, false))
		return true;

	// we are in a blocked situation, so we have to regenerate the gradient
	switch (kind)
	{
		case AreaKind::Guard: updateGuardAreasGradient(teamNumber, swimClass); break;
		case AreaKind::Clear: updateClearAreasGradient(teamNumber, swimClass); break;
	}
	return false;
}




bool Map::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, int swimClass, Uint32 teamMask, int maximumLength)
{
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + w) & wMask;
	targetY = (targetY + h) & hMask;
	const bool canSwim = swimClass > 0;
	// Step costs and the heuristic are in gradient units (see MapInternal.h).
	const int heuristicStep = minStepCost(swimClass);
	const int maximumCost = maximumLength * GRADIENT_STEP;

	AStarComparator compare(aStarPoints);

	///Priority queues use heaps internally, which I've read is the fastest for A* algorithm
	std::priority_queue<int, std::vector<int>, AStarComparator> openList(compare);
	openList.push((x << hDec) + y);
	aStarPoints[(x << hDec) + y] = AStarAlgorithmPoint(x,y,0,0,0,0,false);

	//These are all the examined points, so that these positions on aStarPoints
	//Can be reset later. Why not reset or re-allocate the whole thing every
	//call? Its slow! Use reserve to avoid doing this multiple times
	aStarExaminedPoints.reserve(maximumLength*2 + 6);
	aStarExaminedPoints.push_back((x << hDec) + y);

	while(!openList.empty())
	{
		///Get the smallest from the heap
		int position = openList.top();
		openList.pop();

		AStarAlgorithmPoint& pos = aStarPoints[position];
		pos.isClosed = true;

		if((pos.x == targetX && pos.y == targetY) || (pos.moveCost > maximumCost))
		{
			break;
		}

		for(int lx=-1; lx<=1; ++lx)
		{
			for(int ly=-1; ly<=1; ++ly)
			{
				int nx = (pos.x + lx + w) & wMask;
				int ny = (pos.y + ly + h) & hMask;
				int n = (nx << hDec) + ny;
				AStarAlgorithmPoint& npos = aStarPoints[n];
				if(npos.isClosed)
				{
					continue;
				}
				else
				{
					int moveCost = pos.moveCost + stepCost(lx, ly, coordToIndex(nx, ny), swimClass);
					int totalCost = moveCost + heuristicStep * warpDistMax(targetX, targetY, nx, ny);

					//If this cell hasn't been examined at all yet
					if(npos.x == -1)
					{
						if(isFreeForGroundUnit(nx, ny, canSwim, teamMask) || (nx == targetX && ny == targetY))
						{
							//If the parent cell is the starting cell, add in the starting direction
							if(pos.dx == 0 && pos.dy == 0)
							{
								npos = AStarAlgorithmPoint(nx, ny, lx, ly, moveCost, totalCost, false);
								openList.push(n);
							}
							//Else, the direction is the same as the parents node
							else
							{
								npos = AStarAlgorithmPoint(nx, ny, pos.dx, pos.dy, moveCost, totalCost, false);
								openList.push(n);
							}
							aStarExaminedPoints.push_back(n);
						}
					}
					//Check if we can improve this cells value by taking this route
					else if(npos.moveCost > moveCost)
					{
						npos.moveCost = moveCost;
						npos.totalCost = totalCost;
						npos.dx = pos.dx;
						npos.dy = pos.dy;
					}
				}
			}
		}
	}

	AStarAlgorithmPoint final = aStarPoints[(targetX << hDec) + targetY];

	//Clear all of the examined points for the next call to this algorithm
	for(unsigned i=0; i<aStarExaminedPoints.size(); ++i)
	{
		aStarPoints[aStarExaminedPoints[i]] = AStarAlgorithmPoint();
	}

	aStarExaminedPoints.clear();

	//It was never examined, thus there is no paths
	if(final.x == -1)
		return false;

	//Input direction of the final square to the unit
	*dx = final.dx;
	*dy = final.dy;
	return true;
}
