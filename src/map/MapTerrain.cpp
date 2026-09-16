// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"

// Terrain editing & rendering: setUMatPos, regenerateMap, lookup

namespace
{
	// The neighbours setUMatPos looks at, in its historical order.
	constexpr int UM_NEIGHBOURS[8][2] = {{0,-1}, {0,1}, {-1,0}, {1,0}, {-1,-1}, {1,-1}, {1,1}, {-1,1}};
}

bool Map::touchesUMTerrain(int x, int y, TerrainType t) const
{
	for (const auto &d : UM_NEIGHBOURS)
		if (getUMTerrain(x+d[0], y+d[1])==t)
			return true;
	return false;
}

// Paints t on an l by l square and repairs the corners around it that may not touch t:
// grass and water meet through sand, and so do cobblestone and water. Ice touches anything.
void Map::setUMatPos(int x, int y, TerrainType t, int l)
{
	bool repairedPrototypeTerrain = false;
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
		{
			for (const auto &d : UM_NEIGHBOURS)
			{
				const int nx=dx+d[0], ny=dy+d[1];
				const TerrainType n=getUMTerrain(nx, ny);
				if ((t==GRASS && n==WATER) || (t==WATER && n==GRASS))
					setUMTerrain(nx, ny, SAND);
				else if ((t==WATER && n==COBBLESTONE) || (t==COBBLESTONE && n==WATER))
				{
					setUMTerrain(nx, ny, SAND);
					repairedPrototypeTerrain = true;
				}
			}
			setUMTerrain(dx,dy,t);
		}
	// Sand repairs nothing among the original terrains, so its redraw can stay one corner
	// tighter; the redrawn area decides how many random tile variants are drawn.
	if (t==SAND && !repairedPrototypeTerrain)
		regenerateMap(x-(l>>1)-1,y-(l>>1)-1,l+1,l+1);
	else
		regenerateMap(x-(l>>1)-2,y-(l>>1)-2,l+3,l+3);
}


void Map::regenerateMap(int x, int y, int w, int h)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
		{
			const TerrainType corners[4] = {getUMTerrain(dx,dy), getUMTerrain(dx+1,dy), getUMTerrain(dx,dy+1), getUMTerrain(dx+1,dy+1)};
			// The prototype terrains have flat tiles and no transition art: a tile with any ice
			// corner is ice, one wholly of cobblestone is cobblestone. Any other tile touching
			// cobblestone is drawn and treated as if the cobblestone corners were sand.
			bool ice = false, cobblestone = true;
			Uint8 base[4];
			for (int i = 0; i < 4; i++)
			{
				ice |= corners[i] == ICE;
				cobblestone &= corners[i] == COBBLESTONE;
				base[i] = Uint8(corners[i] == COBBLESTONE ? SAND : corners[i]);
			}
			if (ice)
				setTerrain(dx, dy, ICE_TILE_FIRST + (syncRand() % 16));
			else if (cobblestone)
				setTerrain(dx, dy, COBBLESTONE_TILE_FIRST + (syncRand() % 16));
			else
				setTerrain(dx, dy, lookup(base[0], base[1], base[2], base[3]));
		}
}

Uint16 Map::baseTerrainTile(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br, unsigned variant)
{
	/*
		Value of vertice's order in square :

		3 -- 2
		|    |
		|    |
		1 -- 0

		The index in the following table is :
		val[0] + val[1]*k + val[2]*k^2 + val[3]*k^3
		where k is the number of different possibilities.

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

	return terrainLookupTable[index][0]+(variant%terrainLookupTable[index][1]);
}

Uint16 Map::lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br) const
{
	return baseTerrainTile(tl, tr, bl, br, syncRand());
}

int Map::prototypeTerrainLayers(int x, int y, Uint16 layers[3]) const
{
	const TerrainType corners[4] = {getUMTerrain(x,y), getUMTerrain(x+1,y), getUMTerrain(x,y+1), getUMTerrain(x+1,y+1)};
	// Masks with tl=8, tr=4, bl=2, br=1, the order the edge sprites are drawn in.
	int ice = 0, cobblestone = 0;
	int counts[3] = {0, 0, 0};
	for (int i = 0; i < 4; i++)
	{
		const int bit = 8 >> i;
		if (corners[i] == ICE)
			ice |= bit;
		else if (corners[i] == COBBLESTONE)
			cobblestone |= bit;
		else
			counts[corners[i]]++;
	}
	if (!ice && !cobblestone)
		return 0;
	// A variant per tile from its position, stable from frame to frame.
	Uint32 h = Uint32(x) * 73856093u ^ Uint32(y) * 19349663u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	int n = 0;
	if (counts[WATER] + counts[SAND] + counts[GRASS] > 0)
	{
		// The ground beneath: the new corners take the commonest original terrain of the tile
		// (sand, then grass, then water on a tie), which their edges then cover.
		TerrainType under = SAND;
		if (counts[GRASS] > counts[under])
			under = GRASS;
		if (counts[WATER] > counts[under])
			under = WATER;
		Uint8 base[4];
		for (int i = 0; i < 4; i++)
			base[i] = Uint8(corners[i] == ICE || corners[i] == COBBLESTONE ? under : corners[i]);
		const Uint16 tile = baseTerrainTile(base[0], base[1], base[2], base[3], h);
		if (tile < 256 || tile >= 272)
			layers[n++] = tile;
	}
	else if (cobblestone)
	{
		// Ice and cobblestone only: cobblestone beneath, the ice's edge over it.
		layers[n++] = COBBLESTONE_TILE_FIRST + h % 16;
		cobblestone = 0;
	}
	if (cobblestone)
		layers[n++] = cobblestone == 15 ? COBBLESTONE_TILE_FIRST + h % 16 : COBBLESTONE_EDGE_FIRST + (cobblestone - 1) * 8 + (h >> 4) % 8;
	if (ice)
		layers[n++] = ice == 15 ? ICE_TILE_FIRST + h % 16 : ICE_EDGE_FIRST + (ice - 1) * 8 + (h >> 8) % 8;
	return n;
}
