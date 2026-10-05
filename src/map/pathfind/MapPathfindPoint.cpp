// SPDX-License-Identifier: GPL-3.0-or-later
// Point-to-point movement searches share Map scratch but require no area fields.
#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "Utilities.h"
#include "field/TerrainMovementCosts.h"
#include "field/AirPathfind.h"
#include <queue>
#include <tuple>

bool Map::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, int swimClass, Uint32 teamMask, int maximumLength)
{
	PERF_SCOPE_TIME(PathPoint);
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + w) & wMask;
	targetY = (targetY + h) & hMask;
	const bool canSwim = swimClass > 0;
	// Step costs and the heuristic are in gradient units (see MapInternal.h).
	const int heuristicStep = hasTerrainMovementModifiers() ? minStepCost(swimClass)
        : (swimClass > 0 ? std::min(GRADIENT_STEP, gradient_kernel::WATER_STEP[swimClass]) : GRADIENT_STEP);
	const int maximumCost = maximumLength * GRADIENT_STEP;

	using Entry = std::tuple<unsigned, int, unsigned>;

	///Priority queues use heaps internally, which I've read is the fastest for A* algorithm
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> openList;
	openList.emplace(0, (x << hDec) + y, 0);
	aStarPoints[(x << hDec) + y] = AStarAlgorithmPoint(x,y,0,0,0,0,false);

	//These are all the examined points, so that these positions on aStarPoints
	//Can be reset later. Why not reset or re-allocate the whole thing every
	//call? Its slow! Use reserve to avoid doing this multiple times
	aStarExaminedPoints.reserve(maximumLength*2 + 6);
	aStarExaminedPoints.push_back((x << hDec) + y);

	while(!openList.empty())
	{
		///Get the smallest from the heap
		const auto [estimate, position, queuedCost] = openList.top();
		openList.pop();

		AStarAlgorithmPoint& pos = aStarPoints[position];
        if (pos.isClosed || pos.moveCost != queuedCost) continue;
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
						if(isFreeForGroundUnit(nx, ny, canSwim, teamMask) ||
                            (nx == targetX && ny == targetY &&
                             (terrainPropertiesAt(nx,ny).walkable || (canSwim && terrainPropertiesAt(nx,ny).swimmable))))
						{
							//If the parent cell is the starting cell, add in the starting direction
							if(pos.dx == 0 && pos.dy == 0)
							{
								npos = AStarAlgorithmPoint(nx, ny, lx, ly, moveCost, totalCost, false);
								openList.emplace(totalCost, n, moveCost);
							}
							//Else, the direction is the same as the parents node
							else
							{
								npos = AStarAlgorithmPoint(nx, ny, pos.dx, pos.dy, moveCost, totalCost, false);
								openList.emplace(totalCost, n, moveCost);
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
                        openList.emplace(totalCost, n, moveCost);
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
	if(final.x == -1 || !final.isClosed || final.moveCost > unsigned(maximumCost))
		return false;

	//Input direction of the final square to the unit
	*dx = final.dx;
	*dy = final.dy;
	return true;
}

// Air units retain direct steering on uniform maps. This path is only used when
// terrain changes air access or travel speed; A* then routes around no-fly cells
// and measures the same destination-entry multiplier as actual movement.
bool Map::pathfindAirPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy)
{
    PERF_SCOPE_TIME(PathPoint);
    *dx = *dy = 0;
    x &= wMask; y &= hMask; targetX &= wMask; targetY &= hMask;
	const unsigned minimum = terrainMinimumAir;
	return field::airRoute(
		w, h, x, y, targetX, targetY, minimum, aStarPoints, aStarExaminedPoints,
		[&](int px, int py) { return isFreeForAirUnit(px, py); },
		[&](int px, int py) { return terrainRegistry().airCost(terrainTypeAt(px, py)); }, dx, dy);
}
