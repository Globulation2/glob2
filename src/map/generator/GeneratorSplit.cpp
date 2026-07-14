// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

#include "boost/integer_traits.hpp"
//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerator.h"
#include "Map.h"
#include "Utilities.h"

int MapGenerator::splitUpPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights)
{
	std::vector<MapGeneratorPoint> startingPoints;
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x]==areaN)
			{
				startingPoints.push_back(MapGeneratorPoint(x, y));
			}
		}
	}
	
	if(startingPoints.empty())
		return 0;
	
	Uint32 n = syncRand() % startingPoints.size();

	std::vector<MapGeneratorPoint> obstacles;
	getAllOtherPoints(game, grid, areaN, obstacles);
	std::vector<MapGeneratorPoint> sources;
	sources.push_back(startingPoints[n]);
	std::vector<int> heights;
	computeDistances(game, sources, obstacles, heights);
	sources.clear();
	
	for(unsigned int i=0; i<points.size(); ++i)
	{
		int max = 0;
		std::vector<MapGeneratorPoint> possible;
		for(int x=0; x<game.map.getW(); ++x)
		{
			for(int y=0; y<game.map.getH(); ++y)
			{
				int h = heights[y * game.map.getW() + x];
				if(h > max)
				{
					max = h;
					possible.clear();
				}
				if(h >= max)
				{
					possible.push_back(MapGeneratorPoint(x, y));
				}
			}
		}
		int n = syncRand() % possible.size();
		points[i] = possible[n];
		sources.push_back(points[i]);
		computeDistances(game, sources, obstacles, heights);
	}
	startingPoints.clear();
	heights.clear();
	sources.clear();
	obstacles.clear();
	
	bool cont=true;
	int minDist = boost::integer_traits<int>::const_max;
	while(cont)
	{
		minDist = boost::integer_traits<int>::const_max;
		bool changed=false;
		for(unsigned int i=0; i<points.size(); ++i)
		{
			int best = boost::integer_traits<int>::const_max;
			for(unsigned int j=0; j<points.size(); ++j)
			{
				if(i == j)
					continue;
				int dist=game.map.warpDistSquare(points[i].x, points[i].y, points[j].x, points[j].y) * weights[j];
				best = std::min(dist, best);
			}
			minDist = std::min(best, minDist);
			int orig = best;
			int best_x = -1;
			int best_y = -1;
			for(int dx=-3; dx<=3; ++dx)
			{
				for(int dy=-3; dy<=3; ++dy)
				{
					if(dx==0 && dy==0)
						continue;
					int nx = game.map.normalizeX(points[i].x + dx);
					int ny = game.map.normalizeY(points[i].y + dy);
					if(grid[ny * game.map.getW() + nx]  != areaN)
						continue;
					int score=boost::integer_traits<int>::const_max;
					bool invalid=false;
					for(unsigned int j=0; j<points.size(); ++j)
					{
						if(i == j)
							continue;
						if(nx == points[j].x && ny == points[j].y)
						{
							invalid=true;
							break;
						}
						int dist=game.map.warpDistSquare(nx, ny, points[j].x, points[j].y) * weights[j];
						score = std::min(dist, score);
					}
					if(invalid)
						continue;
					//std::cout<<"dx="<<dx<<", dy="<<dy<<": score="<<score<<std::endl;
					
					if(score>best)
					{
						best = score;
						best_x = nx;
						best_y = ny;
					}
				}
			}
			if(best_x != -1)
			{
				if(best != orig)
					changed=true;
				points[i].x = best_x;
				points[i].y = best_y;
			}
		}
		if(!changed)
		{
			cont = false;
		}
	}
	
	for(unsigned int i=0; i<points.size(); ++i)
	{
		for(unsigned int j=0; j<points.size(); ++j)
		{
			if(i!=j && points[i].x == points[j].x && points[i].y == points[j].y)
				return 0;
		}
	}
	// std::random_shuffle was removed in C++17; std::shuffle takes a URBG directly,
	// which boost::mt19937 satisfies. Note this is map-generation RNG (not syncRand),
	// so cross-machine determinism does not apply here.
	std::shuffle(points.begin(), points.end(), randomGenerator);
	return int(std::sqrt(double(minDist)));
}




void MapGenerator::splitUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights, std::vector<int>& areaNumbers, bool grassOnly)
{
	std::vector<int> gradient(game.map.getW() * game.map.getH(), 0);
	
	Uint32 wDec = game.map.wDec;
	Uint32 hMask = game.map.hMask;
	Uint32 wMask = game.map.wMask;

	std::vector<std::list<int> > squares(points.size());
	std::vector<int> expansion(points.size(), 0);
	std::vector<int> current;
	std::vector<int> count;
	
	for(unsigned int i=0; i<points.size(); ++i)
	{
		grid[points[i].y * game.map.getW() + points[i].x] = i;
		gradient[points[i].y << wDec | points[i].x] = 1;
		squares[i].push_back(points[i].y << wDec | points[i].x);
		
		current.push_back(1);
		count.push_back(1);
	}
	
	
	bool cont=true;
	while(cont)
	{
		bool found=false;
		for(unsigned int p=0; p<points.size(); ++p)
		{
			expansion[p]+=weights[p];
			if(!squares[p].empty())
				found=true;
			while(expansion[p] > 0 && !squares[p].empty())
			{
				Uint32 deltaAddrG = squares[p].back();
				squares[p].erase(--squares[p].end());

				size_t y = deltaAddrG >> wDec;      // Calculate the coordinates of
				size_t x = deltaAddrG & wMask;      // the current field and of the
				
				size_t yu = ((y - 1) & hMask);      // fields next to it.
				size_t yd = ((y + 1) & hMask);      // We live on a torus! If we are on
				size_t xl = ((x - 1) & wMask);      // the "last line" of the map, the
				size_t xr = ((x + 1) & wMask);      // next line is the line 0 again.
				
				
				int t = grid[(y << wDec) | x];
				assert(t < (int)points.size());
				int g = gradient[(y << wDec) | x] + 1;
				grid[(y << wDec) | x] = areaNumbers[t];
				
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
				
				if(g != current[p])
				{
					current[p] = g;
					count[p] = 0;
				}
				
				for (int ci=0; ci<8; ci++)          // Check for each of this fields if we
				{                                   // can improve its gradient value
					addr = &gradient[deltaAddrC[ci]];
					side = *addr;
					if (side==0 && grid[deltaAddrC[ci]]==areaN)
					{
						if(grassOnly && !game.map.isGrass(deltaAddrC[ci]))
							continue;
						*addr = g;
						grid[deltaAddrC[ci]] = t;
						count[p]+=1;
						expansion[p]-=1;
						
						Uint32 randLocation = syncRand() % count[p];
						std::list<int>::iterator i = squares[p].begin();
						std::advance(i, randLocation);
						squares[p].insert(i, deltaAddrC[ci]);
					}
				}
			}
		}
		if(!found)
			cont = false;
	}
}



