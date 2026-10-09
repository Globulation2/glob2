// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// The resource-fraying pass the random generators share. Kept out of
// MapTerrain.cpp on purpose: smoothResources needs globalContainer's resource types, and the test
// build links MapTerrain.cpp against a stripped server-mode libgag that has no globalContainer
// (test/SConstruct).
#include "GlobalContainer.h"
#include "Map.h"
#include "Game.h"
#include "MapInternal.h"
#include "Unit.h"
#include "Utilities.h"

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
	invalidateResourceSeeds();
	for (int s=0; s<times; s++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				Resource &r=resourceCells[coordToIndex(x, y)].resource;
				if (r.type==NO_RES_TYPE || !resourcePropertiesByIndex(r.type).smoothPlacement)
					continue;
				if (!(privateRandom(RandomDomain::ResourceSmoothing).nextU32()&4))
					continue;
				const auto& definition = resourceRegistry().yields(static_cast<ResourceId>(r.type));
				const auto material = resourcePropertiesByIndex(r.type).primaryMaterial;
				const auto& yield = definition[materialIndex(material)];
				if (int(r.amount)-RESOURCE_INITIAL_AMOUNT<=int(privateRandom(RandomDomain::ResourceSmoothing).nextU32()&3))
				{
					if (r.amount<yield.capacity)
						setMaterialAmountSlot(coordToIndex(x,y), materialIndex(material), r.amount+1);
				}
				else
				{
					int dx, dy;
					Unit::dxDyFromDirection(privateRandom(RandomDomain::ResourceSmoothing).nextU32()&7, &dx, &dy);
					const int nx=normalizeX(x+dx), ny=normalizeY(y+dy);
					if (getResource(nx, ny).type==NO_RES_TYPE && isResourceAllowed(nx, ny, r.type))
					{
						Resource sprout;
						sprout.type=r.type;
						sprout.variety=0;
						sprout.amount=RESOURCE_INITIAL_AMOUNT;
						sprout.animation=0;
						replaceResource(nx,ny,sprout);
					}
				}
			}
}
