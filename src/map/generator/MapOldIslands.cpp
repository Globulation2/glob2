// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerationDescriptor.h"
#include "Map.h"
#include "Utilities.h"

bool Map::oldMakeIslandsMap(MapGenerationDescriptor &descriptor)
{
	// First, fill with water:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			undermap[y*w+x]=WATER;
		
	// Two, plants "bootstraps"
	int* bootX=descriptor.bootX;
	int* bootY=descriptor.bootY;
	int nbIslands=descriptor.nbTeams;
	int islandsSize=(int)(((w+h)*descriptor.oldIslandSize)/(400.0*sqrt((double)nbIslands)));
	if (islandsSize<8)
		islandsSize=8;
	int minDistSquare=(w*h)/nbIslands;
	
	
	int c=0;
	for (int i=0; i<nbIslands; i++)
	{
		int x=syncRand()%w;
		int y=syncRand()%h;
		bool failed=false;
		int j;
		for (j=0; j<i; j++)
			if (warpDistSquare(x, y, bootX[j], bootY[j])<minDistSquare)
			{
				failed=true;
				break;
			}
		if (failed)
		{
			
			i--;
			if (c++>65536)
			{
				minDistSquare=minDistSquare>>1;
				//I think that you need to do this only once, in worst case.
				//With a few luck you doesn't need to.
				c=0;
				
			}
		}
		else
		{
			bootX[i]=x;
			bootY[i]=y;
			for (int dx=-1; dx<6; dx++)
				for (int dy=0; dy<6; dy++)
					setUMTerrain(x+dx, y+dy, GRASS);
		}
	}
	
	
	
	// Three, expands islands
	for (int s=0; s<islandsSize; s++)
	{
		for (int oddEven=0; oddEven<2; oddEven++)
		{
			for (int y=oddEven; y<h; y+=2)
			{
				for (int x=oddEven; x<w; x+=2)
				{
					TerrainType umt=getUMTerrain(x, y);
					if (umt==GRASS)
						continue;

					int a, b;
					switch (syncRand()&15)
					{
					case 0:
						a=getUMTerrain(x+1, y);
						b=getUMTerrain(x-1, y);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 1:
						a=getUMTerrain(x, y-1);
						b=getUMTerrain(x, y+1);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 2:
						a=getUMTerrain(x+1, y+1);
						b=getUMTerrain(x-1, y-1);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 3:
						a=getUMTerrain(x+1, y-1);
						b=getUMTerrain(x-1, y+1);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 4:
						a=getUMTerrain(x+2, y);
						b=getUMTerrain(x-2, y);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 5:
						a=getUMTerrain(x, y-2);
						b=getUMTerrain(x, y+2);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 6:
						a=getUMTerrain(x+2, y+2);
						b=getUMTerrain(x-2, y-2);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					case 7:
						a=getUMTerrain(x+2, y-2);
						b=getUMTerrain(x-2, y+2);
						if ((a==GRASS)||(b==GRASS))
						{
							setUMTerrain(x, y, GRASS);
						}
					break;
					default:
					break;
					}
				}
			}
		}
	}
	
	// Four, avoid too much sand. Let's smooth
	for (int s=0; s<2; s++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				int a, b;
				a=getUMTerrain(x+1, y);
				b=getUMTerrain(x-1, y);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
				a=getUMTerrain(x, y-1);
				b=getUMTerrain(x, y+1);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
				a=getUMTerrain(x+1, y+1);
				b=getUMTerrain(x-1, y-1);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
				a=getUMTerrain(x+1, y-1);
				b=getUMTerrain(x-1, y+1);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
			}
	
	controlSand();
	
	// Five, add some sand
	for (int s=0; s<descriptor.oldBeach; s++)
		for (int dy=0; dy<4; dy++)
			for (int dx=0; dx<4; dx++)
				for (int y=dy; y<h; y+=4)
					for (int x=dx; x<w; x+=4)
					{
						int a, b;
						switch (syncRand()&7)
						{
						case 0:
							a=getUMTerrain(x+1, y);
							b=getUMTerrain(x-1, y);
							if (((a==SAND)&&(b==WATER))||((a==WATER)&&(b==SAND)))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 1:
							a=getUMTerrain(x, y-1);
							b=getUMTerrain(x, y+1);
							if (((a==SAND)&&(b==WATER))||((a==WATER)&&(b==SAND)))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 2:
							a=getUMTerrain(x+1, y+1);
							b=getUMTerrain(x-1, y-1);
							if (((a==SAND)&&(b==WATER))||((a==WATER)&&(b==SAND)))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 3:
							a=getUMTerrain(x+1, y-1);
							b=getUMTerrain(x-1, y+1);
							if (((a==SAND)&&(b==WATER))||((a==WATER)&&(b==SAND)))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						
						
						case 4:
							a=getUMTerrain(x+1, y);
							b=getUMTerrain(x-1, y);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 5:
							a=getUMTerrain(x, y-1);
							b=getUMTerrain(x, y+1);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 6:
							a=getUMTerrain(x+1, y+1);
							b=getUMTerrain(x-1, y-1);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 7:
							a=getUMTerrain(x+1, y-1);
							b=getUMTerrain(x-1, y+1);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						}
				}
	
	
	//controlSand();
	regenerateMap(0, 0, w, h);
	return true;
}

