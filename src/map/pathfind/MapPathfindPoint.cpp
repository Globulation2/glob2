// SPDX-License-Identifier: GPL-3.0-or-later
// Point-to-point movement searches share Map scratch but require no area fields.
#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "Utilities.h"
#include "field/TerrainMovementCosts.h"
#include <queue>
#include <tuple>

bool Map::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, int swimClass, Uint32 teamMask, int maximumLength)
{
	PERF_SCOPE_TIME(PathPoint);
	*dx = *dy = 0;
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + w) & wMask;
	targetY = (targetY + h) & hMask;
	// Step costs and the heuristic are in gradient units (see MapInternal.h).
    const int heuristicStep=hasMovementCostModifiers(swimClass) ? minStepCost(swimClass)
        : (gradient_kernel::weightedClass(swimClass) ? std::min(GRADIENT_STEP,gradient_kernel::WATER_STEP[swimClass]) : GRADIENT_STEP);
    if(maximumLength<0) return false;
    const int maximumCost=int(std::min<std::int64_t>(GRADIENT_COST_LIMIT,std::int64_t(maximumLength)*GRADIENT_STEP));
    const bool air=swimClass==AIR_MOVEMENT_CLASS;
    const auto blockedAirGoal=[&] {
        const auto i=coordToIndex(targetX,targetY);
        return !terrainPropertiesAt(i).flyable || resourceBlocksAir(i) || getAirUnit(targetX,targetY)!=NOGUID;
    };
	const bool groundInteraction =
		!air && !terrainAllowsMovementAt(targetX, targetY, swimClass) &&
		(getGroundUnit(targetX, targetY) != NOGUID || getBuilding(targetX, targetY) != NOGBID ||
		 getResource(targetX, targetY).type != NO_RES_TYPE);
	// An occupied interaction target can be reached from an adjacent legal
	// cell without ever treating its water/rock terrain as a transit square.
	const bool approach = (air && blockedAirGoal()) || groundInteraction;
	AStarAlgorithmPoint reached;

	using Entry = std::tuple<unsigned, int, unsigned>;

	///Priority queues use heaps internally, which I've read is the fastest for A* algorithm
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> openList;
	openList.emplace(0, (x << hDec) + y, 0);
	aStarPoints[(x << hDec) + y] = AStarAlgorithmPoint(x,y,0,0,0,0,false);

	//These are all the examined points, so that these positions on aStarPoints
	//Can be reset later. Why not reset or re-allocate the whole thing every
	//call? Its slow! Use reserve to avoid doing this multiple times
	aStarExaminedPoints.reserve(std::min(size,std::size_t(maximumCost)*2+6));
	aStarExaminedPoints.push_back((x << hDec) + y);

	while(!openList.empty())
	{
		///Get the smallest from the heap
		const auto [estimate, position, queuedCost] = openList.top();
		openList.pop();

		AStarAlgorithmPoint& pos = aStarPoints[position];
        if (pos.isClosed || pos.moveCost != queuedCost) continue;
		pos.isClosed = true;

		if((pos.x == targetX && pos.y == targetY) || (approach && warpDistMax(pos.x,pos.y,targetX,targetY)<=1))
		{
			reached=pos;
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
					const bool allowed = canTraverse(pos.x, pos.y, lx, ly, swimClass, teamMask) ||
										 (!air && nx == targetX && ny == targetY &&
										  canTraverse(pos.x, pos.y, lx, ly, swimClass, teamMask,
													  Map::DestinationAccess::InteractionEndpoint));
					if (!allowed)
						continue; // Revisited nodes must obey the same edge rules.
					int moveCost = pos.moveCost + stepCost(lx, ly, coordToIndex(nx, ny), swimClass);
					if (moveCost > maximumCost) continue;
					int totalCost = moveCost + heuristicStep * warpDistMax(targetX, targetY, nx, ny);

					//If this cell hasn't been examined at all yet
					if(npos.x == -1)
					{
						if (allowed)
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
bool Map::pathfindAirPointToPoint(int x,int y,int targetX,int targetY,int* dx,int* dy)
{
    return pathfindPointToPoint(x,y,targetX,targetY,dx,dy,AIR_MOVEMENT_CLASS,0,GRADIENT_COST_LIMIT/GRADIENT_STEP);
}
