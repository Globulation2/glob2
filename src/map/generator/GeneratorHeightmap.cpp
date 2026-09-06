// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "GlobalContainer.h"
#include "HeightMapGenerator.h"
#include "MapGenerator.h"
#include "Map.h"
#include <queue>
#include "Unit.h"
#include "Utilities.h"

void MapGenerator::adjustHeightmapFromPoints(Game& game, std::vector<MapGeneratorPoint>& points, std::vector<int>& heightmap, int value)
{
	for(unsigned int i=0; i<points.size(); ++i)
	{
		heightmap[points[i].y * game.map.getW() + points[i].x] += value;
	}
}



void MapGenerator::adjustHeightmapFromPerlinNoise(Game& game, std::vector<int>& heights, int spread)
{
	HeightMap noise(game.map.getW(), game.map.getH());
	noise.makePlain(4);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			heights[y * game.map.getW() + x] += noise.uiLevel(x, y, spread*2) - spread;
		}
	}
}



void MapGenerator::computeDistances(Game& game, std::vector<MapGeneratorPoint>& sources, std::vector<MapGeneratorPoint>& obstacles, std::vector<int>& heightmap)
{
	std::queue<int> places;
	heightmap.clear();
	heightmap.resize(game.map.getW() * game.map.getH(), 0);
	for(unsigned int i=0; i<sources.size(); ++i)
	{
		heightmap[sources[i].y * game.map.getW() + sources[i].x] = 1;
		places.push(sources[i].y * game.map.getW() + sources[i].x);
	}
	for(unsigned int i=0; i<obstacles.size(); ++i)
	{
		heightmap[obstacles[i].y * game.map.getW() + obstacles[i].x] = -1;
	}
	
	Uint32 wDec = game.map.wDec;
	Uint32 hMask = game.map.hMask;
	Uint32 wMask = game.map.wMask;
	while (!places.empty())
	{
		int deltaAddrG = places.front();
		places.pop();
		
		size_t y = deltaAddrG >> wDec;      // Calculate the coordinates of
		size_t x = deltaAddrG & wMask;      // the current field and of the
		
		size_t yu = ((y - 1) & hMask);      // fields next to it.
		size_t yd = ((y + 1) & hMask);      // We live on a torus! If we are on
		size_t xl = ((x - 1) & wMask);      // the "last line" of the map, the
		size_t xr = ((x + 1) & wMask);      // next line is the line 0 again.
		
		int g = heightmap[(y << wDec) | x] + 1;
		
		size_t deltaAddrC[8];
		int *addr;
		int side;
		
		deltaAddrC[0] = (yu << wDec) | xl;  // Calculate the positions of the
		deltaAddrC[1] = (yu << wDec) | x ;  // 8 fields next to us from their
		deltaAddrC[2] = (yu << wDec) | xr;  // coordinates.
		deltaAddrC[3] = (y  << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x ;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y  << wDec) | xl;
		for (int ci=0; ci<8; ci++)          // Check for each of this fields if we
		{                                   // can improve its gradient value
			addr = &heightmap[deltaAddrC[ci]];
			side = *addr;
			if (side==0)
			{
				*addr = g;
				places.push(deltaAddrC[ci]);
			}
		}
	}
}



int MapGenerator::computeAverageDistance(Game& game, std::vector<int>& grid, int areaN, const std::vector<int>& heightmap)
{
	long total = 0;
	int count = 0;
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x] == areaN)
			{
				total += heightmap[y * game.map.getW() + x];
				count+=1;
			}
		}
	}
	return count > 0 ? total/count : 0;
}



Building* MapGenerator::addBuilding(Game& game, int x, int y, int team, int type, int level, bool underConstruction)
{
	std::string name = IntBuildingType::typeFromShortNumber(type);
	int typeNum=globalContainer->buildingsTypes.getTypeNum(name, level-1, underConstruction);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	if(bt == NULL)
	{
		return NULL;
	}

	if (game.checkRoomForBuilding(x, y, bt, team, false))
	{
		if(bt->maxUnitWorking)
			return game.addBuilding(x, y, typeNum, team, 1, 0);
		else
			return game.addBuilding(x, y, typeNum, team, 0, 0);
	}
	return NULL;
}



