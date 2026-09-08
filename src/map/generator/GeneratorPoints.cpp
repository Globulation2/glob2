// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerator.h"
#include "Map.h"
#include "Unit.h"
#include "Utilities.h"

void MapGenerator::getAllPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
    getAllPointsTask(game, grid, areaN, points).run();
}

GAGCore::CooperativeTask MapGenerator::getAllPointsTask(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	for(int x=0;  x<game.map.getW(); ++x)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for(int y=0; y<game.map.getH(); ++y)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			if(grid[y * game.map.getW() + x] == areaN)
				points.push_back(MapGeneratorPoint(x, y));
		}
	}
    co_return true;
}



void MapGenerator::getAllOtherPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
    getAllOtherPointsTask(game, grid, areaN, points).run();
}

GAGCore::CooperativeTask MapGenerator::getAllOtherPointsTask(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	for(int x=0;  x<game.map.getW(); ++x)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for(int y=0; y<game.map.getH(); ++y)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			if(grid[y * game.map.getW() + x] != areaN)
				points.push_back(MapGeneratorPoint(x, y));
		}
	}
    co_return true;
}



void MapGenerator::getAllPointsLine(Game& game, int x1, int y1, int x2, int y2, std::vector<MapGeneratorPoint>& points)
{
    getAllPointsLineTask(game, x1, y1, x2, y2, points).run();
}

GAGCore::CooperativeTask MapGenerator::getAllPointsLineTask(Game& game, int x1, int y1, int x2, int y2, std::vector<MapGeneratorPoint>& points)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	int startX = x1;
	int endX = x2;
	int startY = y1;
	int endY = y2;
	
	int dirX = (endX > startX ? 1 : -1);
	int distX = std::abs(endX - startX);
	if(distX > game.map.getW()/2)
	{
		dirX = -dirX;
		distX = game.map.getW() -  distX;
	}
			
	int dirY = (endY > startY ? 1 : -1);
	int distY = std::abs(endY - startY);
	if(distY > game.map.getH()/2)
	{
		dirY = -dirY;
		distY = game.map.getH() -  distY;
	}
			
	if(distX > distY)
	{
		int px = 0;
		int py = 0;
		int y = startY;
		for(int x=startX; x!=endX;)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			px+=1;
			points.push_back(MapGeneratorPoint(x, y));
			if(std::abs(px * distY - py * distX) > std::abs(px * distY - (py+1) * distX))
			{
				y=game.map.normalizeY(y+dirY);
				points.push_back(MapGeneratorPoint(x, y));
				py+=1;
			}
			x=game.map.normalizeX(x+dirX);
		}
	}
	else
	{
		int px = 0;
		int py = 0;
		int x = startX;
		for(int y=startY; y!=endY;)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			py+=1;
			points.push_back(MapGeneratorPoint(x, y));
			if(std::abs(py * distX - px * distY) > std::abs(py * distX - (px+1) * distY))
			{
				x=game.map.normalizeX(x+dirX);
				points.push_back(MapGeneratorPoint(x, y));
				px+=1;
			}
			y=game.map.normalizeY(y+dirY);
		}
	}
    co_return true;
}



void MapGenerator::findBorderPoints(Game& game, std::vector<int>& grid, std::vector<MapGeneratorPoint>& points)
{
    findBorderPointsTask(game, grid, points).run();
}

GAGCore::CooperativeTask MapGenerator::findBorderPointsTask(Game& game, std::vector<int>& grid, std::vector<MapGeneratorPoint>& points)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	for(int x=0; x<game.map.getW(); ++x)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for(int y=0; y<game.map.getH(); ++y)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			bool found=false;
			for(int dx=-1; dx<=1 && !found; ++dx)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				for(int dy=-1; dy<=1 && !found; ++dy)
				{
					if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
					if(grid[game.map.normalizeY(y + dy) * game.map.getW() + game.map.normalizeX(x + dx)] != grid[y * game.map.getW() + x])
					{
						found=true;
					}
				}
			}
			if(found)
				points.push_back(MapGeneratorPoint(x, y));
		}
	}
    co_return true;
}



void MapGenerator::fillInResource(Game& game, std::vector<MapGeneratorPoint>& points, int resourceType, int maxFillSize)
{
    fillInResourceTask(game, points, resourceType, maxFillSize).run();
}

GAGCore::CooperativeTask MapGenerator::fillInResourceTask(Game& game, std::vector<MapGeneratorPoint>& points, int resourceType, int maxFillSize)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	for(unsigned int n=0;  n<points.size(); ++n)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		game.map.setResource(points[n].x, points[n].y, resourceType, 1+syncRand()%maxFillSize);
	}
    co_return true;
}



void MapGenerator::chooseRandomPoints(Game& game, std::vector<MapGeneratorPoint>& points, int n)
{
    chooseRandomPointsTask(game, points, n).run();
}

GAGCore::CooperativeTask MapGenerator::chooseRandomPointsTask(Game& game, std::vector<MapGeneratorPoint>& points, int n)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	n = std::min(int(points.size()), n);
	for(int i=0; i<n; ++i)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		int r = syncRand() % (points.size()-i);
		std::iter_swap(points.begin() + i, points.begin() + i + r);
	}
	points.erase(points.begin() + n, points.end());
    co_return true;
}



void MapGenerator::chooseFreeForBuildingSquares(Game& game, std::vector<MapGeneratorPoint>& points, BuildingType* type, int team)
{
    chooseFreeForBuildingSquaresTask(game, points, type, team).run();
}

GAGCore::CooperativeTask MapGenerator::chooseFreeForBuildingSquaresTask(Game& game, std::vector<MapGeneratorPoint>& points, BuildingType* type, int team)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	std::vector<MapGeneratorPoint> newPoints;
	for(unsigned int n=0; n<points.size(); ++n)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		if(game.checkRoomForBuilding(points[n].x, points[n].y, type, team, false))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points.swap(newPoints);
    co_return true;
}



void MapGenerator::chooseFreeForGroundUnits(Game& game, std::vector<MapGeneratorPoint>& points, int team)
{
    chooseFreeForGroundUnitsTask(game, points, team).run();
}

GAGCore::CooperativeTask MapGenerator::chooseFreeForGroundUnitsTask(Game& game, std::vector<MapGeneratorPoint>& points, int team)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	std::vector<MapGeneratorPoint> newPoints;
	for(unsigned int n=0; n<points.size(); ++n)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		if(game.map.isFreeForGroundUnit(points[n].x, points[n].y, false, 1<<team))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points.swap(newPoints);
    co_return true;
}



void MapGenerator::chooseTouchingBuilding(Game& game, std::vector<MapGeneratorPoint>& points, Building* building)
{
    chooseTouchingBuildingTask(game, points, building).run();
}

GAGCore::CooperativeTask MapGenerator::chooseTouchingBuildingTask(Game& game, std::vector<MapGeneratorPoint>& points, Building* building)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	std::vector<MapGeneratorPoint> newPoints;
	for(unsigned int n=0; n<points.size(); ++n)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		if(game.map.doesPosTouchBuilding(points[n].x, points[n].y, building->gid))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points.swap(newPoints);
    co_return true;
}



