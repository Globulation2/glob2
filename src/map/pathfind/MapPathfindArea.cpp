/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/


#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


// Area pathfinding (forbidden, guard, clear, point-to-point)

bool Map::pathfindForbidden(const Uint8 *optionGradient, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy, bool verbose)
{
	if (verbose)
		printf("pathfindForbidden(%d, %d, (%d, %d))\n", teamNumber, canSwim, x, y);
	pathfindForbiddenCount++;
	Uint8 *gradient=forbiddenGradient[teamNumber][canSwim];
	if (verbose && !gradient)
		printf("error, Map::pathfindForbidden(), forbiddenGradient[teamNumber=%d][canSwim=%d] is NULL\n", teamNumber, canSwim);
	assert(gradient);
	
	Uint32 maxValue=0;
	int maxd=0;
	for (int di=0; di<8; di++)
	{
		int rx=tabClose[di][0];
		int ry=tabClose[di][1];
		int xg=(x+rx)&wMask;
		int yg=(y+ry)&hMask;
		if (verbose)
			printf("[di=%d], r=(%d, %d), g=(%d, %d)\n", di, rx, ry, xg, yg);
		if (!isFreeForGroundUnitNoForbidden(xg, yg, canSwim))
			continue;
		size_t addr=xg+(yg<<wDec);
		Uint8 base=gradient[addr];
		if (verbose)
			printf("gradient[%d]=%d\n", static_cast<unsigned>(addr), gradient[addr]);
		Uint8 option;
		if (optionGradient!=NULL)
			option=optionGradient[addr];
		else
			option=0;
		if (verbose)
			printf("option=%d @ %p\n", option, optionGradient);
		Uint32 value=(base<<8)|option;
		if (verbose)
			printf("value=%d \n", value);
		if (maxValue<value)
		{
			maxValue=value;
			if (verbose)
				printf("new maxValue=%d \n", maxValue);
			maxd=di;
		}
	}
	if (maxValue>=(2<<8))
	{
		*dx=tabClose[maxd][0];
		*dy=tabClose[maxd][1];
		if (verbose)
			printf(" Success (%d:%d) (%d, %d)\n", (maxValue>>8), (maxValue&0xFF), *dx, *dy);
		pathfindForbiddenCountSuccess++;
		return true;
	}
	else
	{
		if (verbose)
			printf(" Failure (%d)\n", maxValue);
		pathfindForbiddenCountFailure++;
		return false;
	}
}

bool Map::pathfindGuardArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy)
{
	Uint8 *gradient = guardAreasGradient[teamNumber][canSwim];
	Uint8 max = gradient[x + (y<<wDec)];
	if (max == 255)
		return false; // we already are in an area.
	if (max < 2)
		return false; // any existing area are too far away.
	bool found = false;
	
	// we look around us, searching for a usable position with a bigger gradient value 
	if (directionByMinigrad(1<<teamNumber, canSwim, x, y, dx, dy, gradient, true, verbose))
	{
		found = true;
	}
	
	// we are in a blocked situation, so we have to regenerate the forbidden gradient
	if (!found)
		updateGuardAreasGradient(teamNumber, canSwim);
	
	return found;
}



bool Map::pathfindClearArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy)
{
	Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
	Uint8 max = gradient[x + (y<<wDec)];
	if (max == 255)
		return false; // we already are in an area.
	if (max < 2)
		return false; // any existing area are too far away.
	bool found = false;
	
	// we look around us, searching for a usable position with a bigger gradient value 
	if (directionByMinigrad(1<<teamNumber, canSwim, x, y, dx, dy, gradient, true, verbose))
	{
		found = true;
	}
	
	// we are in a blocked situation, so we have to regenerate the forbidden gradient
	if (!found)
		updateClearAreasGradient(teamNumber, canSwim);
	
	return found;
}




bool Map::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, bool canSwim, Uint32 teamMask, int maximumLength)
{
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + w) & wMask;
	targetY = (targetY + h) & hMask;
	
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
				
		if((pos.x == targetX && pos.y == targetY) || (pos.moveCost > maximumLength))
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
					int moveCost = pos.moveCost + 1;
					int totalCost = moveCost +  warpDistMax(targetX, targetY, nx, ny);
					
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




