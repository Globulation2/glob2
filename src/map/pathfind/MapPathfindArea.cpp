// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "MapInternal.h"
#include "PathfindStats.h"
#include "PathfindPolicy.h"

#include <queue>


// Area pathfinding (forbidden, guard, clear, point-to-point)

bool Map::pathfindForbidden(const Uint8 *optionGradient, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy, int swimClass)
{
	Uint8 *gradient=forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
	if (PathfindPolicy::useAlternative(teamNumber))
	{
		if (swimClass < 0)
			swimClass = canSwim ? DEFAULT_SWIM_CLASS : 0;
		activeSwimClasses[teamNumber] |= 1u << swimClass;
		const Uint16 *cost = forbiddenCost[teamNumber][swimClass];
		if (cost != NULL)
		{
			Uint32 teamMask = Team::teamNumberToMask(teamNumber);
			if (directionByCost(teamMask, swimClass, x, y, cost, dx, dy, true, true))
				return !(*dx == 0 && *dy == 0);
			return directionByCost(teamMask, swimClass, x, y, cost, dx, dy, false, true);
		}
	}

	// Pick the neighbor with the highest (base, option) lexicographically. The base gradient
	// dominates; the option gradient is used as a tiebreaker. Reject results where the chosen
	// base is unreachable (i.e. require base > GRADIENT_UNREACHABLE).
	Uint8 bestBase = 0;
	Uint8 bestOption = 0;
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
		Uint8 base=gradient[addr];
		Uint8 option = (optionGradient!=NULL) ? optionGradient[addr] : 0;
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

bool Map::pathfindArea(AreaKind kind, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy, int swimClass)
{
	if (PathfindPolicy::useAlternative(teamNumber))
	{
		if (swimClass < 0)
			swimClass = canSwim ? DEFAULT_SWIM_CLASS : 0;
		activeSwimClasses[teamNumber] |= 1u << swimClass;
		const Uint16 *cost = (kind == AreaKind::Guard)
			? guardAreasCost[teamNumber][swimClass]
			: clearAreasCost[teamNumber][swimClass];
		if (cost != NULL)
		{
			Uint16 here = cost[coordToIndex(x, y)];
			if (here == 0 || here == COST_INFINITY)
				return false;
			Uint32 teamMask = Team::teamNumberToMask(teamNumber);
			if (directionByCost(teamMask, swimClass, x, y, cost, dx, dy, true))
				return true;
			if (directionByCost(teamMask, swimClass, x, y, cost, dx, dy, false))
				return true;
			switch (kind)
			{
				case AreaKind::Guard: updateGuardAreasGradient(teamNumber, canSwim); break;
				case AreaKind::Clear: updateClearAreasGradient(teamNumber, canSwim); break;
			}
			return false;
		}
	}
	Uint8 *gradient = (kind == AreaKind::Guard)
		? guardAreasGradient[teamNumber][canSwim]
		: clearAreasGradient[teamNumber][canSwim];
	Uint8 max = gradient[x + (y<<wDec)];
	if (max == GRADIENT_AT_GOAL)
		return false; // we already are in an area.
	if (max < 2)
		return false; // any existing area is too far away.

	// we look around us, searching for a usable position with a bigger gradient value
	if (directionByMinigrad(1<<teamNumber, canSwim, x, y, dx, dy, gradient, true))
		return true;

	// we are in a blocked situation, so we have to regenerate the gradient
	switch (kind)
	{
		case AreaKind::Guard: updateGuardAreasGradient(teamNumber, canSwim); break;
		case AreaKind::Clear: updateClearAreasGradient(teamNumber, canSwim); break;
	}
	return false;
}




bool Map::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, bool canSwim, Uint32 teamMask, int maximumLength, int swimClass)
{
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + w) & wMask;
	targetY = (targetY + h) & hMask;

	PathfindStats::Scope pfScope(PathfindStats::get().pointToPoint);
	// Weighted (octile + terrain) costs and a heap with a total order for
	// units on the alternative pathfinder; the baseline keeps unit costs.
	const bool weighted = swimClass >= 0;
	const int heuristicUnit = weighted ? minStepCost(swimClass) : 1;
	const int maxCost = weighted ? maximumLength * 10 : maximumLength;
	AStarComparator compare(aStarPoints, weighted);

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

		if((pos.x == targetX && pos.y == targetY) || (pos.moveCost > maxCost))
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
					int moveCost = pos.moveCost + (weighted ? weightedStepCost(lx, ly, coordToIndex(nx, ny), swimClass) : 1);
					int totalCost = moveCost + heuristicUnit * warpDistMax(targetX, targetY, nx, ny);

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
	PathfindStats::get().pointToPointExpanded += aStarExaminedPoints.size();

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
