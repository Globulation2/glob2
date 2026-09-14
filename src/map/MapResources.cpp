// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <vector>



// Resource grid mutations + resource availability + points/area names

void Map::decResource(int x, int y)
{
	Resource &r = getTile(x, y).resource;
	
	if (r.type == NO_RES_TYPE || r.amount == 0)
		return;
	
	const ResourceType *fulltype = globalContainer->resourcesTypes.get(r.type);
	
	if (!fulltype->shrinkable)
		return;
	if (fulltype->eternal)
	{
		if (r.amount > 0)
			r.amount--;
	}
	else
	{
		if (!fulltype->granular || r.amount<=1)
			r.clear();
		else
			r.amount--;
	}
}

void Map::decResource(int x, int y, int resourceType)
{
	if (isResourceTakeable(x, y, resourceType))
		decResource(x, y);
}

// Radius of the terrain probe in Map::growResources: it draws dwax and dway as
// (syncRand()&0xF)-(syncRand()&0xF), so each lands anywhere in [-15,15] and the
// probe can reach any tile of the 31x31 box around the source.
static constexpr int GROWTH_PROBE_RADIUS = 15;

// The grain a farmed tile keeps back. decResource clears a granular tile at one
// grain, so a pooled harvest that took the last one would leave bare ground and
// the farm would have protected nothing.
static constexpr int FARM_SEED_AMOUNT = 1;

bool Map::canResourceEverGrowHere(int x, int y, int resourceType) const
{
	if (resourceType == NO_RES_TYPE)
		return false;
	const ResourceType *type = globalContainer->resourcesTypes.get(resourceType);
	if (getTerrainType(x, y) != type->terrain)
		return false;

	// Every gated resource needs water somewhere in the probe box. Scanning out
	// from the tile finds it on the first ring for anything near a shore, and
	// only runs the full box for the tiles that are about to be rejected.
	bool water = false;
	for (int r = 0; r <= GROWTH_PROBE_RADIUS && !water; r++)
		for (int dy = -r; dy <= r && !water; dy++)
			for (int dx = -r; dx <= r; dx++)
			{
				if (std::max(std::abs(dx), std::abs(dy)) != r)
					continue;
				if (isWater(x + dx, y + dy))
				{
					water = true;
					break;
				}
			}
	if (!water)
		return false;

	// Algae also need sand, at twice the offsets, so their box is twice as wide
	// and only covers even offsets.
	if (resourceType == ALGA)
	{
		for (int dy = -GROWTH_PROBE_RADIUS; dy <= GROWTH_PROBE_RADIUS; dy++)
			for (int dx = -GROWTH_PROBE_RADIUS; dx <= GROWTH_PROBE_RADIUS; dx++)
				if (isSand(x + dx * 2, y + dy * 2))
					return true;
		return false;
	}
	return true;
}

int Map::farmCropAt(int x, int y) const
{
	switch (getTerrainType(x, y))
	{
		case GRASS: return WHEAT;
		case WATER: return ALGA;
		default:    return NO_RES_TYPE;
	}
}

bool Map::isClearingTarget(size_t index, Uint32 teamMask) const
{
	const Tile &tile = tiles[index];
	if (tile.resource.type == NO_RES_TYPE)
		return false;
	if (!globalContainer->resourcesTypes.get(tile.resource.type)->clearable)
		return false;
	if (tile.clearArea & teamMask)
		return true;
	if ((tile.farmArea & teamMask) == 0)
		return false;
	// Inside a farm, everything clearable except the crop the terrain grows.
	return tile.resource.type != farmCropAt(static_cast<int>(index & wMask),
	                                        static_cast<int>(index >> wDec));
}

bool Map::canPaintFarmArea(int x, int y) const
{
	if (!canResourcesGrow(x, y))
		return false;

	const Resource &resource = getTile(x, y).resource;
	// Wood shares wheat's terrain, so a forest inside a wheat farm is paintable:
	// the farm clears it and grows into it. Stone, papyrus and the fruits are
	// never farmed and stone and the fruits cannot even be removed.
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
		const Tile &tile = tiles[index];
		return (tile.farmArea & teamMask) != 0
			&& tile.resource.type == resourceType
			&& tile.resource.amount > 0;
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
	// the 8 neighbours. The queue is a member vector reused across harvests, and
	// the stamps keep every tile on it once -- including the 3x3 seeds, which a
	// map narrow enough to wrap can otherwise contribute twice.
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
		// side of the field it stands on; then the tile index, which is the
		// tie-break that makes the choice the same on every client.
		//
		// A tile at FARM_SEED_AMOUNT is this field's seed and is never taken:
		// decResource would clear it, and a farm that can be reduced to bare
		// ground is not protecting anything. A field worked past its surplus
		// therefore stalls at one grain a tile and regrows, and the workers
		// that arrive meanwhile go home empty. Tiles at the seed amount still
		// carry the flood, so the field does not split as it is worked down.
		const Sint32 amount = tiles[index].resource.amount;
		const Sint32 distance = warpDistSquare(x, y, tx, ty);
		if (amount > FARM_SEED_AMOUNT
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
	// can have emptied while the unit played its 32-tick harvest animation, and
	// a farm has to keep working across that.
	if (isFarmArea(x + dx, y + dy, teamMask) && isFarmableResource(resourceType))
	{
		const std::optional<size_t> source = pickFarmHarvestTile(x, y, resourceType, teamMask);
		if (!source)
			return false;
		decResource(static_cast<int>(*source & wMask), static_cast<int>(*source >> wDec), resourceType);
		return true;
	}

	// Off a farm, master's behaviour byte for byte, phantom grain included: the
	// unit is granted a resource whether or not the tile still had one.
	decResource(x + dx, y + dy, resourceType);
	return true;
}

bool Map::incResource(int x, int y, int resourceType, int variety)
{
	Resource &r = getTile(x, y).resource;
	const ResourceType *fulltype;
	if (r.type == NO_RES_TYPE)
	{
		if (getBuilding(x, y) != NOGBID)
			return false;
		if (getGroundUnit(x, y) != NOGUID)
			return false;

		fulltype = globalContainer->resourcesTypes.get(resourceType);
		if (getTerrainType(x, y) == fulltype->terrain)
		{
			r.type = resourceType;
			r.variety = variety;
			r.amount = RESOURCE_INITIAL_AMOUNT;
			r.animation = 0;
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
		return true;
	}
	else
	{
		r.amount--;
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
			tiles[coordToIndex(dx, dy)].resource.clear();
}

void Map::removeUnallowedResources(int x, int y, int w, int h)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
		{
			Resource& r=tiles[coordToIndex(dx, dy)].resource;
			if (r.type!=NO_RES_TYPE && getTerrainType(dx, dy)!=globalContainer->resourcesTypes.get(r.type)->terrain)
				r.clear();
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
				Resource& rp=tiles[coordToIndex(dx, dy)].resource;
				rp.type=type;
				const ResourceType *rt=globalContainer->resourcesTypes.get(type);
				rp.variety=syncRand()%rt->varietiesCount;
				assert(rt->sizesCount>1);
				rp.amount=RESOURCE_INITIAL_AMOUNT+syncRand()%(rt->sizesCount-1);
				rp.animation=0;
			}
}

bool Map::isResourceAllowed(int x, int y, int type)
{
	return (getBuilding(x, y) == NOGBID) && (getGroundUnit(x, y) == NOGUID) && (getTerrainType(x, y)==globalContainer->resourcesTypes.get(type)->terrain);
}

bool Map::isPointSet(int n, int x, int y) const
{
	return getTile(x, y).scriptAreas & 1<<n;
}

void Map::setPoint(int n, int x, int y)
{
	getTile(x, y).scriptAreas |= 1<<n;
}

void Map::unsetPoint(int n, int x, int y)
{
	getTile(x, y).scriptAreas ^= getTile(x, y).scriptAreas & (1<<n);
}

std::string Map::getAreaName(int n) const
{
	return areaNames[n];
}

void Map::setAreaName(int n, std::string name)
{
	areaNames[n]=name;
}


bool Map::resourceAvailable(int teamNumber, int resourceType, int swimClass, int x, int y)
{
	Uint16 g = getGradient(teamNumber, resourceType, swimClass, x, y);
	return g>GRADIENT_UNREACHABLE; //Because 0==obstacle, 1==no obstacle, but you don't know if there is anything around.
}

bool Map::resourceAvailable(int teamNumber, int resourceType, int swimClass, int x, int y, int *dist)
{
	Uint16 g = getGradient(teamNumber, resourceType, swimClass, x, y);
	if (g>GRADIENT_UNREACHABLE)
	{
		*dist = gradientTiles(g);
		return true;
	}
	else
		return false;
}

bool Map::resourceAvailableUpdate(int teamNumber, int resourceType, int swimClass, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist)
{
	// distance and availability
	bool result;
	if (dist)
		result = resourceAvailable(teamNumber, resourceType, swimClass, x, y, dist);
	else
		result = resourceAvailable(teamNumber, resourceType, swimClass, x, y);
		
	// target position
	const Uint16 *gradient = getResourceGradient(teamNumber, resourceType, swimClass);
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



