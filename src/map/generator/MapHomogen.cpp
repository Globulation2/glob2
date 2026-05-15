// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <float.h>
#include <time.h>
#include <stdlib.h>
#include <vector>

#include "boost/integer_traits.hpp"
#include "boost/integer/common_factor.hpp"
//also the Perlin Noise stuff uses random that is not based on syncRand
#include "boost/random.hpp"
#include "Game.h"
#include "GlobalContainer.h"
#include "HeightMapGenerator.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
#include <map>
#include <queue>
#include <set>
#include "Unit.h"
#include "Utilities.h"

///generates a map that is of one terrain type only
void Map::makeHomogenMap(TerrainType terrainType)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			undermap[y*w+x]=terrainType;
	regenerateMap(0, 0, w, h);
}

///cares for the sand so water is never next to grass
void Map::controlSand(void)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int tt=(int)undermap[y*w+x];
			switch (tt)
			{
				case WATER: //the direct neighbours get checked for GRASS
					for (int dy=-1; dy<=1; dy++)
						for (int dx=-1; dx<=1; dx++)
							if (getUMTerrain(x+dx, y+dy)==GRASS)
								undermap[y*w+x]=SAND;
					break;
				case SAND:
					break;
				case GRASS: //the direct neighbours get checked for WATER
					for (int dy=-1; dy<=1; dy++)
						for (int dx=-1; dx<=1; dx++)
							if (getUMTerrain(x+dx, y+dy)==WATER)
								undermap[y*w+x]=SAND;
					break;
			}
		}
}

void Map::smoothRessources(int times)
{
	for (int s=0; s<times; s++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				int d=getTerrain(x, y)-272;
				int r=d/10;
				int l=d%5;
				if ((d>=0)&&(d<40)&&(syncRand()&4))
				{
					if (l<=(int)(syncRand()&3))
						setTerrain(x, y, d+273);
					else 
					{
						// we extand ressource:
						int dx, dy;
						Unit::dxDyFromDirection(syncRand()&7, &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						if (getGroundUnit(nx, ny)==NOGUID)
							if (((r==WOOD||r==CORN||r==STONE)&&isGrass(nx, ny))||((r==ALGA)&&isWater(nx, ny)))
								setTerrain(nx, ny, 272+(r*10)+((syncRand()&1)*5));
					}
				}
			}
}

void simulateRandomMap(int smooth, double baseWater, double baseSand, double baseGrass, double *finalWater, double *finalSand, double *finalGrass)
{
	int w=32<<(smooth>>2);
	int h=w;
	int s=w*h;
	int m=s-1;
	std::vector<int> undermap(w*h);
	
	int totalRatio=0x7FFF;
	int waterRatio=(int)(baseWater*((double)totalRatio));
	int sandRatio =(int)(baseSand *((double)totalRatio));
	int grassRatio=(int)(baseGrass*((double)totalRatio));
	totalRatio=waterRatio+sandRatio+grassRatio;
	
	if(totalRatio==0)
	{
		waterRatio = 1;
		sandRatio = 1;
		grassRatio = 1;
		totalRatio = 3;
	}
	
	
	/// First, we create a fully random patchwork:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int r=syncRand()%totalRatio;
			r-=waterRatio;
			if (r<0)
			{
				undermap[y*w+x]=0;
				continue;
			}
			r-=sandRatio;
			if (r<0)
			{
				undermap[y*w+x]=1;
				continue;
			}
			r-=grassRatio;
			if (r<0)
			{
				undermap[y*w+x]=2;
				continue;
			}
			assert(false);//Want's to sing ?
		}
	
	for (int i=0; i<smooth; i++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+1+s)&m];
					int b=undermap[(y*w+x-1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w+s)&m];
					int b=undermap[(y*w+x-w+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w+1+s)&m];
					int b=undermap[(y*w+x-w-1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w-1+s)&m];
					int b=undermap[(y*w+x-w+1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w-2+s)&m];
					int b=undermap[(y*w+x-w+2+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w-(h<<1)+s)&m];
					int b=undermap[(y*w+x-w+(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w+2+(h<<1)+s)&m];
					int b=undermap[(y*w+x-w-2-(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w+2-(h<<1)+s)&m];
					int b=undermap[(y*w+x-w-2+(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
			}
	// What's finally in ?
	int waterCount=0;
	int sandCount =0;
	int grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			switch (undermap[y*w+x])
			{
				case 0:
					waterCount++;
				continue;
				case 1:
					sandCount ++;
				continue;
				case 2:
					grassCount++;
				continue;
			}	
	int totalCount=waterCount+sandCount+grassCount;
	
	*finalWater=((double)waterCount)/((double)totalCount);
	*finalSand =((double)sandCount )/((double)totalCount);
	*finalGrass=((double)grassCount)/((double)totalCount);
}

void fastSimulateRandomMap(int smooth, double baseWater, double baseSand, double baseGrass, double *finalWater, double *finalSand, double *finalGrass)
{
	int n=smooth*2+1;
	std::vector<double> finalWaters(n);
	std::vector<double> finalSands(n);
	std::vector<double> finalGrasses(n);
	
	for (int i=0; i<n; i++)
		simulateRandomMap(4, baseWater, baseSand, baseGrass, &finalWaters[i], &finalSands[i], &finalGrasses[i]);
	
	double medianFinalWater=finalWaters[0];
	double medianFinalSand =finalSands [0];
	double medianFinalGrass=finalGrasses[0];
	for (int i=0; i<n; i++)
	{
		double wf=finalWaters[i];
		double sf=finalSands [i];
		double gf=finalGrasses[i];
		int ws=0;
		int wb=0;
		int we=0;
		int ss=0;
		int sb=0;
		int se=0;
		int gs=0;
		int gb=0;
		int ge=0;
		for (int j=0; j<n; j++)
		{
			if (wf<finalWaters[j])
				wb++;
			else if (wf>finalWaters[j])
				ws++;
			else
				we++;
			if (sf<finalSands[j])
				sb++;
			else if (sf>finalSands[j])
				ss++;
			else
				se++;
			if (gf<finalGrasses[j])
				gb++;
			else if (gf>finalGrasses[j])
				gs++;
			else
				ge++;
		}
		if (abs(wb-ws)<we)
			medianFinalWater=wf;
		if (abs(sb-ss)<se)
			medianFinalSand=sf;
		if (abs(gb-gs)<ge)
			medianFinalGrass=gf;
	}
	*finalWater=medianFinalWater;
	*finalSand=medianFinalSand;
	*finalGrass=medianFinalGrass;
}

