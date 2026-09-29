// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// The whole-map passes the generators share: a uniform terrain to start from, the sand rule that
// keeps water off grass, and the resource-fraying pass. Kept out of MapTerrain.cpp on purpose:
// smoothResources needs globalContainer's resource types, and the test build links MapTerrain.cpp
// against a stripped server-mode libgag that has no globalContainer (test/SConstruct).
#include "GlobalContainer.h"
#include "Map.h"
#include "MapInternal.h"
#include "Unit.h"
#include "Utilities.h"

///generates a map that is of one terrain type only
void Map::makeHomogenMap(TerrainType terrainType)
{
    makeHomogenMapTask(terrainType).run();
}
GAGCore::CooperativeTask Map::makeHomogenMapTask(TerrainType terrainType)
{
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) undermap[y*w+x] = terrainType;
        if (y % 8 == 0) co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
    }
    for (int x = 0; x < w; ++x) {
        regenerateMap(x, 0, 1, h);
        if (x % 8 == 0) co_await GAGCore::CooperativeTask::checkpoint();
    }
    co_return true;
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

// The random generators' closing pass (nuage, 2002): the resource growth step run over the whole map
// `times` times, without its water test, to fray the square deposits setResource stamps into
// natural fields. Each wood, wheat, stone or algae tile, half the time, either grows (the smaller
// it is, the likelier) or, once it is large, sprouts a small tile of its kind on a random free
// neighbour of the right terrain. Tiles are visited in row order and updated in place, as the
// original did, so a sprout can grow again later in the same pass.
//
// When resources moved out of the terrain values in 2003 this kept reading the old encoding
// (terrain values of 272 and up) and silently stopped doing anything; this is the same rule over
// the Resource struct.
void Map::smoothResources(int times)
{
	for (int s=0; s<times; s++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				Resource &r=tiles[coordToIndex(x, y)].resource;
				if (r.type!=WOOD && r.type!=WHEAT && r.type!=STONE && r.type!=ALGA)
					continue;
				if (!(syncRand()&4))
					continue;
				const ResourceType *rt=globalContainer->resourcesTypes.get(r.type);
				if (int(r.amount)-RESOURCE_INITIAL_AMOUNT<=int(syncRand()&3))
				{
					if (r.amount<rt->sizesCount)
						r.amount++;
				}
				else
				{
					int dx, dy;
					Unit::dxDyFromDirection(syncRand()&7, &dx, &dy);
					const int nx=normalizeX(x+dx), ny=normalizeY(y+dy);
					if (getResource(nx, ny).type==NO_RES_TYPE && isResourceAllowed(nx, ny, r.type))
					{
						Resource &sprout=tiles[coordToIndex(nx, ny)].resource;
						sprout.type=r.type;
						sprout.variety=syncRand()%rt->varietiesCount;
						sprout.amount=RESOURCE_INITIAL_AMOUNT;
						sprout.animation=0;
					}
				}
			}
}
