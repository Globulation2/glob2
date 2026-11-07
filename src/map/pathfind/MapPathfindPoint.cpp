// SPDX-License-Identifier: GPL-3.0-or-later
// Point-to-point movement searches share Map scratch but require no area fields.
#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/MapRouting.h"
#include "Game.h"
#include "gradient/GradientRuntime.h"
#include <bit>
#include "MapInternal.h"
#include "Utilities.h"
#include "field/TerrainMovementCosts.h"
#include <queue>
#include <tuple>

bool MapRouting::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, MovementAccess movementAccessValue, Uint32 teamMask, int maximumLength)
{
    int swimClass=accessClass(movementAccessValue);
    const int physical=swimClass;
    const int readerTeam=map.game && std::has_single_bit(teamMask)?int(std::countr_zero(teamMask)):-1;
    if(readerTeam>=0) {
        swimClass=preparationCostClass(readerTeam,physical);
    }
	PERF_SCOPE_TIME(PathPoint);
	*dx = *dy = 0;
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + map.w) & map.wMask;
	targetY = (targetY + map.h) & map.hMask;
	// Step costs and the heuristic are in gradient units (see MapInternal.h).
    const int heuristicStep=map.hasMovementCostModifiers(swimClass) ? minStepCost(swimClass)
        : (gradient_kernel::weightedClass(swimClass) ? std::min(GRADIENT_STEP,gradient_kernel::WATER_STEP[swimClass]) : GRADIENT_STEP);
    if(maximumLength<0) return false;
    const int maximumCost=int(std::min<std::int64_t>(Map::GRADIENT_COST_LIMIT,std::int64_t(maximumLength)*GRADIENT_STEP));
    const bool air=swimClass==AIR_MOVEMENT_CLASS;
    const auto blockedAirGoal=[&] {
        const auto i=map.coordToIndex(targetX,targetY);
        return !map.terrainPropertiesAt(i).flyable || map.resourceBlocksAir(i) || map.getAirUnit(targetX,targetY)!=NOGUID;
    };
    const bool approach=air && blockedAirGoal();
    AStarAlgorithmPoint reached;

	using Entry = std::tuple<unsigned, int, unsigned>;

	///Priority queues use heaps internally, which I've read is the fastest for A* algorithm
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> openList;
	openList.emplace(0, (x << map.hDec) + y, 0);
	aStarPoints[(x << map.hDec) + y] = AStarAlgorithmPoint(x,y,0,0,0,0,false);

	//These are all the examined points, so that these positions on aStarPoints
	//Can be reset later. Why not reset or re-allocate the whole thing every
	//call? Its slow! Use reserve to avoid doing this multiple times
	aStarExaminedPoints.reserve(std::min(map.size,std::size_t(maximumCost)*2+6));
	aStarExaminedPoints.push_back((x << map.hDec) + y);

	while(!openList.empty())
	{
		///Get the smallest from the heap
		const auto [estimate, position, queuedCost] = openList.top();
		openList.pop();

		AStarAlgorithmPoint& pos = aStarPoints[position];
        if (pos.isClosed || pos.moveCost != queuedCost) continue;
		pos.isClosed = true;

		if((pos.x == targetX && pos.y == targetY) || (approach && map.warpDistMax(pos.x,pos.y,targetX,targetY)<=1))
		{
			reached=pos;
			break;
		}

		for(int lx=-1; lx<=1; ++lx)
		{
			for(int ly=-1; ly<=1; ++ly)
			{
				int nx = (pos.x + lx + map.w) & map.wMask;
				int ny = (pos.y + ly + map.h) & map.hMask;
				int n = (nx << map.hDec) + ny;
				AStarAlgorithmPoint& npos = aStarPoints[n];
				if(npos.isClosed)
				{
					continue;
				}
				else
				{
					int moveCost = pos.moveCost + stepCost(lx, ly, map.coordToIndex(nx, ny), swimClass);
					if (moveCost > maximumCost) continue;
					int totalCost = moveCost + heuristicStep * map.warpDistMax(targetX, targetY, nx, ny);

					//If this cell hasn't been examined at all yet
					if(npos.x == -1)
					{
                        if(map.isFreeForMovement(nx,ny,swimClass,teamMask) ||
                            (!air && nx==targetX && ny==targetY && gradient_kernel::terrainAllowsGround(map.terrainPropertiesAt(nx,ny),swimClass)))
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

	AStarAlgorithmPoint final = reached;

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

// Compatibility entry point for existing callers. All movement uses the same A*.
bool MapRouting::pathfindAirPointToPoint(int x,int y,int targetX,int targetY,int* dx,int* dy)
{
    return pathfindPointToPoint(x,y,targetX,targetY,dx,dy,MovementAccess::Air,0,Map::GRADIENT_COST_LIMIT/GRADIENT_STEP);
}
