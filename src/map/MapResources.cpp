// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "gradient/ResourceSeedCache.h"
#include "Utilities.h"
#include "ExperimentalFeatures.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapInternal.h"
#include "TerrainResourceProperties.h"

#include <algorithm>
#include <cstdlib>
#include <limits>



// Resource grid mutations + resource availability + points/area names

void Map::decResource(int x, int y)
{
	const size_t index = coordToIndex(x, y);
	Resource &r = resourceCells[index].resource;
	
	if (r.type == NO_RES_TYPE || r.amount == 0)
		return;
	
	const ResourceType *fulltype = globalContainer->resourcesTypes.get(r.type);
	
	if (!fulltype->shrinkable)
		return;
	++snapshotResources;
	if (fulltype->eternal)
	{
		if (r.amount > 0)
			r.amount--;
	}
	else
	{
		if (!fulltype->granular || r.amount<=1)
		{
			r.clear();
			resourceSeedChanged(index, ResourceSeedCache::Resource);
		}
		else
			r.amount--;
	}
}

void Map::decResource(int x, int y, int resourceType)
{
	if (isResourceTakeable(x, y, resourceType))
		decResource(x, y);
}

namespace
{
bool terrainSupportsResource(const TerrainProperties &terrain, int resourceType)
{
	if (resourceType < 0 || resourceType >= MAX_NB_RESOURCES ||
	    (terrain.allowedResources & (1u << resourceType)) == 0)
		return false;
	const ResourceType *resource = globalContainer->resourcesTypes.get(resourceType);
	return ::terrainSupportsResource(terrain, resourceType, resource->shrinkable);
}
}

bool Map::terrainSupportsResourceAt(int x, int y, int resourceType) const
{
	return terrainSupportsResource(terrainPropertiesAt(x, y), resourceType);
}

// The grain a farmed tile keeps back. decResource clears a granular tile at one
// grain, so a pooled harvest that took the last one would leave bare ground and
// the farm would have protected nothing.
static constexpr int FARM_SEED_AMOUNT = 1;

bool Map::farmAreasEnabled() const
{
	return game && game->gameHeader.hasExperiment(ExperimentId::FarmAreas);
}

bool Map::canResourceEverGrowHere(int x, int y, int resourceType) const
{
	const auto &terrain = terrainPropertiesAt(x, y);
	if (!terrain.resourcesGrow || !terrainSupportsResource(terrain, resourceType))
		return false;
	return resourceGrowthField().rate(coordToIndex(x, y), resourceType) != 0;
}

int Map::farmCropAt(int x, int y) const
{
	return terrainPropertiesAt(x, y).farmCrop;
}

bool Map::isClearingTarget(size_t index, Uint32 teamMask, bool farmAreas) const
{

	if (resourceCells[index].resource.type == NO_RES_TYPE)
		return false;
	if (!globalContainer->resourcesTypes.get(resourceCells[index].resource.type)->clearable)
		return false;
	if (areaCells[index].clear & teamMask)
		return true;
	if (!farmAreas || (areaCells[index].farm & teamMask) == 0)
		return false;
	// Inside a farm, everything clearable except the crop the terrain grows.
	return resourceCells[index].resource.type != farmCropAt(static_cast<int>(index & wMask),
	                                        static_cast<int>(index >> wDec));
}

bool Map::canPaintFarmArea(int x, int y) const
{
	if (!canResourcesGrow(x, y))
		return false;

	const Resource &resource = getResource(x, y);
	// Wood shares wheat's terrain, so a forest inside a wheat farm is paintable:
	// the farm clears it and grows into it. Stone, papyrus and the fruits are
	// never farmed, and stone and the fruits cannot even be removed.
	if (resource.type != NO_RES_TYPE
		&& resource.type != WHEAT && resource.type != WOOD && resource.type != ALGA)
		return false;

	const int crop = farmCropAt(x, y);
	return crop != NO_RES_TYPE && canResourceEverGrowHere(x, y, crop);
}

bool Map::isFarmableResource(int resourceType) const
{
	if (resourceType == NO_RES_TYPE)
		return false;
	const ResourceType *type = globalContainer->resourcesTypes.get(resourceType);
	return type->granular && type->shrinkable && !type->eternal && type->expendable;
}

std::optional<size_t> Map::pickFarmHarvestTile(int x, int y, int resourceType, Uint32 teamMask)
{
	if (!isFarmableResource(resourceType))
		return std::nullopt;

	// A tile belongs to the field only while it is inside the team's farm area
	// and still holds the resource; an emptied tile drops out and can split the
	// field in two. That is the whole of "no teleportation across empty fields".
	auto inField = [&](size_t index) {

		return (areaCells[index].farm & teamMask) != 0
			&& resourceCells[index].resource.type == resourceType
			&& resourceCells[index].resource.amount > 0;
	};

	// One stamp buffer per map, bumped instead of cleared. Wrapping the counter
	// would make stale stamps read as visited, so the wrap resets the buffer.
	if (farmFloodStamps.size() != size)
	{
		farmFloodStamps.assign(size, 0);
		farmFloodGeneration = 0;
	}
	if (++farmFloodGeneration == 0)
	{
		std::fill(farmFloodStamps.begin(), farmFloodStamps.end(), 0);
		farmFloodGeneration = 1;
	}

	// Flood the field from every tile of the unit's own 3x3, breadth-first over
	// the 8 neighbours. The stamps keep every tile on the queue once, including
	// the 3x3 seeds, which a map narrow enough to wrap can contribute twice.
	std::vector<size_t>& frontier = farmFloodQueue;
	frontier.clear();
	for (int tdy = -1; tdy <= 1; tdy++)
		for (int tdx = -1; tdx <= 1; tdx++)
		{
			const size_t index = coordToIndex(x + tdx, y + tdy);
			if (farmFloodStamps[index] == farmFloodGeneration || !inField(index))
				continue;
			farmFloodStamps[index] = farmFloodGeneration;
			frontier.push_back(index);
		}
	if (frontier.empty())
		return std::nullopt;

	size_t best = frontier.front();
	Sint32 bestAmount = 0;
	Sint32 bestDistance = 0;
	for (size_t head = 0; head < frontier.size(); head++)
	{
		const size_t index = frontier[head];
		const int tx = static_cast<int>(index & wMask);
		const int ty = static_cast<int>(index >> wDec);

		// Ripest first; then the one the unit is nearest, so a worker eats the
		// side of the field it stands on; then the tile index, which makes the
		// choice the same on every client.
		//
		// A tile at FARM_SEED_AMOUNT is this field's seed and is never taken. A
		// field worked past its surplus stalls at one grain a tile and regrows;
		// a worker harvesting it meanwhile gets nothing and harvests again until
		// a tile is back above its seed (Unit::handleDisplacement). Seed tiles
		// still carry the flood, so the field does not split as it is worked down.
		const Sint32 amount = resourceCells[index].resource.amount;
		const Sint32 distance = warpDistSquare(x, y, tx, ty);
		// No regrowth makes every grain finite supply, including the last seed.
		const int seedAmount=game && game->gameHeader.isResourceGrowthDisabled() ? 0 : FARM_SEED_AMOUNT;
		if (amount > seedAmount
			&& (amount > bestAmount
				|| (amount == bestAmount && distance < bestDistance)
				|| (amount == bestAmount && distance == bestDistance && index < best)))
		{
			best = index;
			bestAmount = amount;
			bestDistance = distance;
		}

		for (int tdy = -1; tdy <= 1; tdy++)
			for (int tdx = -1; tdx <= 1; tdx++)
			{
				if (tdx == 0 && tdy == 0)
					continue;
				const size_t neighbour = coordToIndex(tx + tdx, ty + tdy);
				if (farmFloodStamps[neighbour] == farmFloodGeneration || !inField(neighbour))
					continue;
				farmFloodStamps[neighbour] = farmFloodGeneration;
				frontier.push_back(neighbour);
			}
	}
	if (bestAmount == 0)
		return std::nullopt;
	return best;
}

bool Map::takeHarvest(int x, int y, int dx, int dy, int resourceType, Uint32 teamMask)
{
	// The trigger is the painted area, not the resource on it: the target tile
	// can have emptied while the unit played its harvest animation, and a farm
	// has to keep working across that.
	if (isFarmArea(x + dx, y + dy, teamMask) && isFarmableResource(resourceType) && farmAreasEnabled())
	{
		const std::optional<size_t> source = pickFarmHarvestTile(x, y, resourceType, teamMask);
		if (!source)
			return false;
		decResource(static_cast<int>(*source & wMask), static_cast<int>(*source >> wDec), resourceType);
		return true;
	}

	// The original rule, phantom grain included: the unit is granted a
	// resource whether or not the tile still had one.
	decResource(x + dx, y + dy, resourceType);
	return true;
}

bool Map::incResource(int x, int y, int resourceType, int variety)
{
	if (!terrainSupportsResource(terrainPropertiesAt(x,y),resourceType)) return false;
	const size_t index = coordToIndex(x, y);
	Resource &r = resourceCells[index].resource;
	const ResourceType *fulltype;
	if (r.type == NO_RES_TYPE)
	{
		if (getBuilding(x, y) != NOGBID)
			return false;
		if (getGroundUnit(x, y) != NOGUID)
			return false;

		fulltype = globalContainer->resourcesTypes.get(resourceType);
		if (terrainSupportsResource(terrainPropertiesAt(x, y), resourceType))
		{
			r.type = resourceType;
			r.variety = variety;
			r.amount = RESOURCE_INITIAL_AMOUNT;
			r.animation = 0;
			++snapshotResources;
			resourceSeedChanged(index, ResourceSeedCache::Resource);
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		fulltype = globalContainer->resourcesTypes.get(r.type);
	}

	if (r.type != resourceType)
		return false;
	if (!fulltype->shrinkable)
		return false;
	if (r.amount < fulltype->sizesCount)
	{
		r.amount++;
		++snapshotResources;
		return true;
	}
	else
	{
		r.amount--;
		++snapshotResources;
	}
	return false;
}


void Map::setNoResource(int x, int y, int l)
{
	assert(l>=0);
	assert(l<w);
	assert(l<h);
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
			replaceResource(dx, dy, Resource{});
}

void Map::removeUnallowedResources(int x, int y, int w, int h)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
		{
			Resource& r=resourceCells[coordToIndex(dx, dy)].resource;
			if (r.type!=NO_RES_TYPE && !terrainSupportsResource(terrainPropertiesAt(dx, dy), r.type))
				replaceResource(dx, dy, Resource{});
		}
}

void Map::setResource(int x, int y, int type, int l)
{
	assert(l>=0);
	assert(l<w);
	assert(l<h);
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
			if (isResourceAllowed(dx, dy, type))
			{
				Resource& rp=resourceCells[coordToIndex(dx, dy)].resource;
				const bool changedType = rp.type != type;
				rp.type=type;
				const ResourceType *rt=globalContainer->resourcesTypes.get(type);
				rp.variety=syncRand()%rt->varietiesCount;
				assert(rt->sizesCount>1);
				rp.amount=RESOURCE_INITIAL_AMOUNT+syncRand()%(rt->sizesCount-1);
				rp.animation=0;
				++snapshotResources;
				if (changedType) resourceSeedChanged(coordToIndex(dx, dy), ResourceSeedCache::Resource);
			}
}

bool Map::isResourceAllowed(int x, int y, int type)
{
	return (getBuilding(x, y) == NOGBID) && (getGroundUnit(x, y) == NOGUID) && terrainSupportsResource(terrainPropertiesAt(x, y), type);
}

bool Map::isPointSet(int n, int x, int y) const
{
	return scriptAreaCells[coordToIndex(x, y)] & 1<<n;
}

void Map::setPoint(int n, int x, int y)
{
	scriptAreaCells[coordToIndex(x, y)] |= 1<<n;
}

void Map::unsetPoint(int n, int x, int y)
{
	scriptAreaCells[coordToIndex(x, y)] ^= scriptAreaCells[coordToIndex(x, y)] & (1<<n);
}

std::string Map::getAreaName(int n) const
{
	return areaNames[n];
}

void Map::setAreaName(int n, std::string name)
{
	areaNames[n]=name;
}


bool Map::resourceAvailable(int teamNumber, int resourceType, int swimClass, int x, int y, bool withMarkets, const Building* consumer)
{
	Uint16 g = getGradient(teamNumber, resourceType, swimClass, x, y, withMarkets, consumer);
	return g>GRADIENT_UNREACHABLE; //Because 0==obstacle, 1==no obstacle, but you don't know if there is anything around.
}

bool Map::resourceAvailable(int teamNumber, int resourceType, int swimClass, int x, int y, int *dist, bool withMarkets, const Building* consumer)
{
	Uint16 g = getGradient(teamNumber, resourceType, swimClass, x, y, withMarkets, consumer);
	if (g>GRADIENT_UNREACHABLE)
	{
		*dist = gradientTiles(g);
		return true;
	}
	else
		return false;
}

bool Map::resourceAvailableUpdate(int teamNumber, int resourceType, int swimClass, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist, bool withMarkets, const Building* consumer)
{
	// distance and availability
	bool result;
	if (dist)
		result = resourceAvailable(teamNumber, resourceType, swimClass, x, y, dist, withMarkets, consumer);
	else
		result = resourceAvailable(teamNumber, resourceType, swimClass, x, y, withMarkets, consumer);
		
	// target position
	const Uint16 *gradient = getResourceGradient(teamNumber, resourceType, swimClass, withMarkets, consumer);
	getGlobalGradientDestination(gradient, x, y, targetX, targetY);

	return result;
}

template<typename T>
bool Map::getGlobalGradientDestination(const T *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const
{
	const T atGoal = std::numeric_limits<T>::max();
	// we start from our current position
	int vx = x & wMask;
	int vy = y & hMask;
	// max is initialized to gradient value of current position
	T max = gradient[coordToIndex(vx, vy)];
	
	bool result = false;
	// we follow the gradient uphill; every step strictly increases max, so this ends
	while (true)
	{
		bool found = false;
		int vddx = 0;
		int vddy = 0;
		
		// search all directions
		for (int d=0; d<8; d++)
		{
			int ddx = deltaOne[d][0];
			int ddy = deltaOne[d][1];
			T g = gradient[coordToIndex(vx + ddx, vy + ddy)];
			if (g>max)
			{
				max = g;
				vddx = ddx;
				vddy = ddy;
				found = true;
			}
		}
		
		// change position
		vx = (vx+vddx) & wMask;
		vy = (vy+vddy) & hMask;
		
		// if we have reached destination break
		if (max == atGoal)
		{
			result = true;
			break;
		}
		// if we haven't found a suitable direction, we break, but we do not have exact destination
		else if (!found)
			break;
	}
	
	// return best destination and wether it is exact or not
	*targetX = vx;
	*targetY = vy;
	return result;
}

template bool Map::getGlobalGradientDestination<Uint8>(const Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;
template bool Map::getGlobalGradientDestination<Uint16>(const Uint16 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;

template<typename T>
bool Map::isGradientPeak(const T *gradient, int x, int y) const
{
	// A round-trip gradient's goal is seeded at a finite cost, not the type's
	// max the way GRADIENT_AT_GOAL is, so getGlobalGradientDestination's own
	// "reached exact goal" check does not generalize to it. This is the
	// weaker, gradient-agnostic property an ascent target actually needs:
	// no neighbour holds a strictly higher value, so an ascent from anywhere
	// nearby would still stop here.
	size_t index = coordToIndex(x, y);
	T here = gradient[index];
	for (int d=0; d<8; d++)
		if (gradient[coordToIndex(x+deltaOne[d][0], y+deltaOne[d][1])]>here)
			return false;
	return true;
}

template bool Map::isGradientPeak<Uint8>(const Uint8 *gradient, int x, int y) const;
template bool Map::isGradientPeak<Uint16>(const Uint16 *gradient, int x, int y) const;


