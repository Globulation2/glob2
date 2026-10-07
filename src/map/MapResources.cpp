// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "gradient/ResourceSeedCache.h"
#include "gradient/GradientRuntime.h"
#include "Utilities.h"
#include "ExperimentalFeatures.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapInternal.h"
#include "TerrainResourceProperties.h"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <map>
#include <cstdlib>
#include <limits>



// Resource grid mutations + resource availability + points/area names

bool Map::farmAreasEnabled() const
{
	return game && game->gameHeader.hasExperiment(ExperimentId::FarmAreas);
}

bool Map::canResourceEverGrowHereByIndex(int x, int y, int resourceType) const
{
	return MapState::canResourceEverGrowHere(stateView(),x,y,resourceType);
}

int Map::farmCropAt(int x,int y) const
{
    return MapState::farmCropAt(liveCells,x,y);
}

bool Map::isClearingTarget(size_t index, Uint32 teamMask, bool farmAreas) const
{
    if (resourceCells[index].resource.type==NO_RES_TYPE) return false;
    const bool explicitClear=(areaCells[index].clear&teamMask)!=0;
    if (!explicitClear && !(farmAreas && (areaCells[index].farm&teamMask))) return false;
    const auto& properties=resourcePropertiesByIndex(resourceCells[index].resource.type);
    return properties.clearable && (explicitClear || !properties.farmable);
}

bool Map::canPaintFarmArea(int x,int y) const
{
    return MapState::canPaintFarmArea(stateView(),x,y);
}

bool Map::isFarmableResourceByIndex(int resourceType) const
{
    return MapState::isFarmableResource(liveCells,resourceType);
}

std::optional<size_t> Map::pickFarmHarvestTileSlot(int x, int y, int resourceType, Uint32 teamMask)
{
	if (resourceType < 0 || resourceType >= int(MaterialCount)) return std::nullopt;

	// A tile belongs to the field only while it is inside the team's farm area
	// and still holds the resource; an emptied tile drops out and can split the
	// field in two. That is the whole of "no teleportation across empty fields".
	auto inField = [&](size_t index) {
		return (areaCells[index].farm & teamMask) != 0
			&& isFarmableResourceByIndex(resourceCells[index].resource.type)
            && materialAmountAtSlot(index,resourceType)>0
            && resourceRegistry().yields(static_cast<ResourceId>(resourceCells[index].resource.type))[resourceType].consumption != ResourceConsumption::All
            && !resourceRegistry().yields(static_cast<ResourceId>(resourceCells[index].resource.type))[resourceType].destroysDeposit;
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
		// Finite stocks retain their configured seed reserve. A
		// field worked past its surplus stalls at its reserve and regrows;
		// a worker harvesting it meanwhile gets nothing and harvests again until
		// a tile is back above its seed (Unit::handleDisplacement). Seed tiles
		// still carry the flood, so the field does not split as it is worked down.
		const Sint32 amount = materialAmountAtSlot(index,resourceType);
		const Sint32 distance = warpDistSquare(x, y, tx, ty);
		// No regrowth makes every grain finite supply, including the last seed.
        const auto& harvest=resourceRegistry().yields(static_cast<ResourceId>(resourceCells[index].resource.type))[resourceType];
        const int seedAmount=game && game->gameHeader.isResourceGrowthDisabled() ? 0 : harvest.seedReserve;
		// Infinite harvests leave the reserve intact even at the reserve level.
		if ((amount > seedAmount || harvest.consumption == ResourceConsumption::Infinite)
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

bool Map::takeHarvest(int x,int y,int dx,int dy,MaterialId materialId,Uint32 teamMask)
{
    const int material=materialIndex(materialId);
    const auto target=coordToIndex(x+dx,y+dy);
    if (!materialAmountAtSlot(target,material)) return false;
    if (isFarmArea(x+dx,y+dy,teamMask) && farmAreasEnabled())
    {
        const auto resource=resourceCells[target].resource.type;
        if (!isFarmableResourceByIndex(resource)) return harvestMaterial(target,material);
        const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(resource))[material];
        // Destructive harvests cannot pool through a field: harvest the
        // touched deposit directly, including its secondary materials.
        if (yield.consumption==ResourceConsumption::All || yield.destroysDeposit)
            return harvestMaterial(target,material);
        const auto source=pickFarmHarvestTileSlot(x,y,material,teamMask);
        if (source) return harvestMaterial(*source,material);
        return false;
    }
    return harvestMaterial(target,material);
}

bool Map::growResourceStock(size_t index)
{
    const auto& r=resourceCells[index].resource;
    if (r.type==NO_RES_TYPE) return false;
    const auto& yields=resourceRegistry().yields(static_cast<ResourceId>(r.type));
    bool changed=false;
    for (unsigned remaining=resourcePropertiesByIndex(r.type).materialMask;remaining;remaining&=remaining-1)
    {
        const auto m=std::countr_zero(remaining);
        const auto& y=yields[m];
        if (!y.growthRate) continue;
        const auto amount=materialAmountAtSlot(index,m);
        if (amount>=y.capacity) continue;
        const auto increment=Fertility::growthOpportunities(y.growthRate,[]{return syncRand();});
        if (increment) { setMaterialAmountSlot(index,m,std::min<unsigned>(y.capacity,unsigned(amount)+increment)); changed=true; }
    }
    return changed;
}

bool Map::incResource(int x,int y,ResourceId resourceId,int variety)
{
    const int resourceType=resourceIndex(resourceId);
    if (!terrainSupportsResourceAtByIndex(x,y,resourceType)) return false;
    const auto index=coordToIndex(x,y);
    const auto& r=resourceCells[index].resource;
    if (r.type==NO_RES_TYPE)
    {
        const auto& p=resourcePropertiesByIndex(resourceType);
        if ((p.blocksBuilding && getBuilding(x,y)!=NOGBID) || (p.blocksGround && getGroundUnit(x,y)!=NOGUID) || (p.blocksAir && getAirUnit(x,y)!=NOGUID)) return false;
        Resource next;
        next.type=resourceType; next.variety=variety;
        next.amount=resourceRegistry().yields(static_cast<ResourceId>(resourceType))[materialIndex(p.primaryMaterial)].initial;
        replaceResource(index,next);
        return true;
    }
    if (r.type!=resourceType) return false;
    return growResourceStock(index);
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
			if (r.type!=NO_RES_TYPE && !terrainSupportsResourceAtByIndex(dx,dy,r.type))
				replaceResource(dx, dy, Resource{});
		}
}

void Map::setResource(int x,int y,ResourceId resourceId,int l)
{
    const int type=resourceIndex(resourceId);
    assert(l>=0 && l<w && l<h);
    for (int dx=x-(l>>1);dx<x+(l>>1)+1;++dx)
        for (int dy=y-(l>>1);dy<y+(l>>1)+1;++dy)
            if (isResourceAllowed(dx,dy,type))
            {
                Resource r; r.type=type;
                const auto& p=resourcePropertiesByIndex(type);
                const auto& yld=resourceRegistry().yields(static_cast<ResourceId>(type))[materialIndex(p.primaryMaterial)];
                r.amount=yld.initial;
                // Stock is simulation state; variant selection is deterministic presentation.
                if (std::has_single_bit(p.materialMask) && yld.placementMaximum>yld.initial)
                    r.amount+=syncRand()%(yld.placementMaximum-yld.initial+1);
                replaceResource(dx,dy,r);
            }
}

bool Map::isResourceAllowed(int x,int y,int type)
{
    if (!terrainSupportsResourceAtByIndex(x,y,type)) return false;
    const auto& p=resourcePropertiesByIndex(type);
    return (!p.blocksBuilding || getBuilding(x,y)==NOGBID) && (!p.blocksGround || getGroundUnit(x,y)==NOGUID) && (!p.blocksAir || getAirUnit(x,y)==NOGUID);
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


bool Map::materialAvailableSlot(int teamNumber, int resourceType, int swimClass, int x, int y, bool withMarkets, const Building* consumer)
{
	Uint16 g = getGradient(teamNumber, resourceType, swimClass, x, y, withMarkets, consumer);
	return g>GRADIENT_UNREACHABLE; //Because 0==obstacle, 1==no obstacle, but you don't know if there is anything around.
}

bool Map::materialAvailableSlot(int teamNumber, int resourceType, int swimClass, int x, int y, int *dist, bool withMarkets, const Building* consumer)
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

bool Map::materialAvailableUpdateSlot(int teamNumber, int resourceType, int swimClass, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist, bool withMarkets, const Building* consumer)
{
	// distance and availability
	bool result;
	if (dist)
		result = materialAvailableSlot(teamNumber, resourceType, swimClass, x, y, dist, withMarkets, consumer);
	else
		result = materialAvailableSlot(teamNumber, resourceType, swimClass, x, y, withMarkets, consumer);
		
	// target position
	const Uint16 *gradient = getMaterialGradientSlot(teamNumber, resourceType, swimClass, withMarkets, consumer);
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

