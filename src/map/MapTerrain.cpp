// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"

// Terrain editing & rendering: setUMatPos, regenerateMap, lookup

void Map::setUMatPos(int x, int y, TerrainType t, int l)
{
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
		{
			if (t==GRASS)
			{
				if (getUMTerrain(dx,dy-1)==WATER)
				{
// 					setNoRessource(dx, dy-1, 1);
					setUMTerrain(dx,dy-1,SAND);
				}
				if (getUMTerrain(dx,dy+1)==WATER)
				{
// 					setNoRessource(dx, dy+1, 1);
					setUMTerrain(dx,dy+1,SAND);
				}

				if (getUMTerrain(dx-1,dy)==WATER)
				{
// 					setNoRessource(dx-1, dy, 1);
					setUMTerrain(dx-1,dy,SAND);
				}
				if (getUMTerrain(dx+1,dy)==WATER)
				{
// 					setNoRessource(dx+1, dy, 1);
					setUMTerrain(dx+1,dy,SAND);
				}

				if (getUMTerrain(dx-1,dy-1)==WATER)
				{
// 					setNoRessource(dx-1, dy-1, 1);
					setUMTerrain(dx-1,dy-1,SAND);
				}
				if (getUMTerrain(dx+1,dy-1)==WATER)
				{
// 					setNoRessource(dx+1, dy-1, 1);
					setUMTerrain(dx+1,dy-1,SAND);
				}

				if (getUMTerrain(dx+1,dy+1)==WATER)
				{
// 					setNoRessource(dx+1, dy+1, 1);
					setUMTerrain(dx+1,dy+1,SAND);
				}
				if (getUMTerrain(dx-1,dy+1)==WATER)
				{
// 					setNoRessource(dx-1, dy+1, 1);
					setUMTerrain(dx-1,dy+1,SAND);
				}
			}
			else if (t==WATER)
			{
				if (getUMTerrain(dx,dy-1)==GRASS)
				{
// 					setNoRessource(dx, dy-1, 1);
					setUMTerrain(dx,dy-1,SAND);
				}
				if (getUMTerrain(dx,dy+1)==GRASS)
				{
// 					setNoRessource(dx, dy+1, 1);
					setUMTerrain(dx,dy+1,SAND);
				}

				if (getUMTerrain(dx-1,dy)==GRASS)
				{
// 					setNoRessource(dx-1, dy, 1);
					setUMTerrain(dx-1,dy,SAND);
				}
				if (getUMTerrain(dx+1,dy)==GRASS)
				{
// 					setNoRessource(dx+1, dy, 1);
					setUMTerrain(dx+1,dy,SAND);
				}

				if (getUMTerrain(dx-1,dy-1)==GRASS)
				{
// 					setNoRessource(dx-1, dy-1, 1);
					setUMTerrain(dx-1,dy-1,SAND);
				}
				if (getUMTerrain(dx+1,dy-1)==GRASS)
				{
// 					setNoRessource(dx+1, dy-1, 1);
					setUMTerrain(dx+1,dy-1,SAND);
				}

				if (getUMTerrain(dx+1,dy+1)==GRASS)
				{
// 					setNoRessource(dx+1, dy+1, 1);
					setUMTerrain(dx+1,dy+1,SAND);
				}
				if (getUMTerrain(dx-1,dy+1)==GRASS)
				{
// 					setNoRessource(dx-1, dy+1, 1);
					setUMTerrain(dx-1,dy+1,SAND);
				}
			}
			setUMTerrain(dx,dy,t);
		}
	if (t==SAND)
		regenerateMap(x-(l>>1)-1,y-(l>>1)-1,l+1,l+1);
	else
		regenerateMap(x-(l>>1)-2,y-(l>>1)-2,l+3,l+3);
}


void Map::regenerateMap(int x, int y, int w, int h)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
			setTerrain(dx, dy, lookup(getUMTerrain(dx,dy), getUMTerrain(dx+1,dy), getUMTerrain(dx,dy+1), getUMTerrain(dx+1,dy+1)));
}

Uint16 Map::lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br) const
{
	/*
		Value of vertice's order in square :

		3 -- 2
		|    |
		|    |
		1 -- 0

		The index in the following table is :
		val[0] + val[1]*k + val[2]*k^2 + val[3]*k^3
		where k is the number of different possibilites.
		
		H = grass
		S = sand
		E = water
	*/
	const Uint16 terrainLookupTable[81][2] =
	{
		{ 0, 16 },		// H, H, H, H
		{ 80, 8 },		// H, H, H, S
		{ 0, 16 },		// H, H, H, E
		{ 88, 8 },		// H, H, S, H
		{ 48, 8 },		// H, H, S, S
		{ 0, 16 },		// H, H, S, E
		{ 0, 16 },		// H, H, E, H
		{ 0, 16 },		// H, H, E, S
		{ 0, 16 },		// H, H, E, E
		{ 104, 8 },		// H, S, H, H
		{ 64, 8 },		// H, S, H, S
		{ 0, 16 },		// H, S, H, E
		{ 120, 8 },		// H, S, S, H
		{ 32, 8 },		// H, S, S, S
		{ 0, 16 },		// H, S, S, E
		{ 0, 16 },		// H, S, E, H
		{ 0, 16 },		// H, S, E, S
		{ 0, 16 },		// H, S, E, E
		{ 0, 16 },		// H, E, H, H
		{ 0, 16 },		// H, E, H, S
		{ 0, 16 },		// H, E, H, E
		{ 0, 16 },		// H, E, S, H
		{ 0, 16 },		// H, E, S, S
		{ 0, 16 },		// H, E, S, E
		{ 0, 16 },		// H, E, E, H
		{ 0, 16 },		// H, E, E, S
		{ 0, 16 },		// H, E, E, E

		{ 96, 8 },		// S, H, H, H
		{ 112, 8 },		// S, H, H, S
		{ 0, 16 },		// S, H, H, E
		{ 72, 8 },		// S, H, S, H
		{ 40, 8 },		// S, H, S, S
		{ 0, 16 },		// S, H, S, E
		{ 0, 16 },		// S, H, E, H
		{ 0, 16 },		// S, H, E, S
		{ 0, 16 },		// S, H, E, E
		{ 56, 8 },		// S, S, H, H
		{ 24, 8 },		// S, S, H, S
		{ 0, 16 },		// S, S, H, E
		{ 16, 8 },		// S, S, S, H
		{ 128, 16 },	// S, S, S, S
		{ 208, 8 },		// S, S, S, E
		{ 0, 16 },		// S, S, E, H
		{ 216, 8 },		// S, S, E, S
		{ 176, 8 },		// S, S, E, E
		{ 0, 16 },		// S, E, H, H
		{ 0, 16 },		// S, E, H, S
		{ 0, 16 },		// S, E, H, E
		{ 0, 16 },		// S, E, S, H
		{ 232, 8 },		// S, E, S, S
		{ 192, 8 },		// S, E, S, E
		{ 0, 16 },		// S, E, E, H
		{ 240, 8 },		// S, E, E, S
		{ 160, 8 },		// S, E, E, E

		{ 0, 16 },		// E, H, H, H
		{ 0, 16 },		// E, H, H, S
		{ 0, 16 },		// E, H, H, E
		{ 0, 16 },		// E, H, S, H
		{ 0, 16 },		// E, H, S, S
		{ 0, 16 },		// E, H, S, E
		{ 0, 16 },		// E, H, E, H
		{ 0, 16 },		// E, H, E, S
		{ 0, 16 },		// E, H, E, E
		{ 0, 16 },		// E, S, H, H
		{ 0, 16 },		// E, S, H, S
		{ 0, 16 },		// E, S, H, E
		{ 0, 16 },		// E, S, S, H
		{ 224, 8 },		// E, S, S, S
		{ 248, 8 },		// E, S, S, E
		{ 0, 16 },		// E, S, E, H
		{ 200, 8 },		// E, S, E, S
		{ 168, 8 },		// E, S, E, E
		{ 0, 16 },		// E, E, H, H
		{ 0, 16 },		// E, E, H, S
		{ 0, 16 },		// E, E, H, E
		{ 0, 16 },		// E, E, S, H
		{ 184, 8 },		// E, E, S, S
		{ 152, 8 },		// E, E, S, E
		{ 0, 16 },		// E, E, E, H
		{ 144, 8 },		// E, E, E, S
		{ 256, 16 },	// E, E, E, E
	};

	tl=2-tl;
	tr=2-tr;
	bl=2-bl;
	br=2-br;
	int index=tl*27+tr*9+bl*3+br;

	return terrainLookupTable[index][0]+(syncRand()%terrainLookupTable[index][1]);
}


