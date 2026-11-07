// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "gradient/MapRouting.h"
#include "gradient/ResourceSeedCache.h"
#include "gradient/GradientRuntime.h"
#include "Utilities.h"
#include "ExperimentalFeatures.h"
#include "Game.h"
#include "Building.h"
#include "Team.h"
#include "Unit.h"
#include "claims/ActiveRouteRuntime.h"
#include "claims/ClaimPolicy.h"
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

std::optional<size_t> Map::pickFarmTileWith(std::vector<Uint32>& stamps, std::vector<size_t>& queue, Uint32& generation, int x, int y, int resourceType, Uint32 teamMask, std::optional<size_t> entry, std::optional<ResourceClaims::Owner> availableOwner) const
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
	if (stamps.size() != size)
	{
		stamps.assign(size, 0);
		generation = 0;
	}
	if (++generation == 0)
	{
		std::fill(stamps.begin(), stamps.end(), 0);
		generation = 1;
	}

	// Flood the field from every tile of the unit's own 3x3, breadth-first over
	// the 8 neighbours. The stamps keep every tile on the queue once, including
	// the 3x3 seeds, which a map narrow enough to wrap can contribute twice.
	std::vector<size_t>& frontier = queue;
	frontier.clear();
    // A claim resolves a pooled field from its access tile; real harvesting
    // starts from all touched tiles around the unit.
    if (entry && inField(*entry)) {
        stamps[*entry]=generation;
        frontier.push_back(*entry);
    }
	for (int tdy = -1; !entry && tdy <= 1; tdy++)
		for (int tdx = -1; tdx <= 1; tdx++)
		{
			const size_t index = coordToIndex(x + tdx, y + tdy);
			if (stamps[index] == generation || !inField(index))
				continue;
			stamps[index] = generation;
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
        const Uint16 claimReserve=pooledClaimReserve(harvest);
        const ResourceClaims::Claim candidate{Uint32(index),Uint32(entry.value_or(index)),Uint8(resourceType),resourceCells[index].resource.type,false,claimReserve};
        const bool unclaimed=!availableOwner || materialClaimAvailable(*availableOwner,candidate,
            amount>claimReserve?amount-claimReserve:0,harvest.consumption==ResourceConsumption::Infinite);
		if (unclaimed && (amount > seedAmount || harvest.consumption == ResourceConsumption::Infinite)
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
				if (stamps[neighbour] == generation || !inField(neighbour))
					continue;
				stamps[neighbour] = generation;
				frontier.push_back(neighbour);
			}
	}
	if (bestAmount == 0)
		return std::nullopt;
	return best;
}
std::optional<size_t> Map::pickFarmHarvestTileSlot(int x, int y, int resourceType, Uint32 teamMask, std::optional<size_t> entry, std::optional<ResourceClaims::Owner> availableOwner)
{
	return pickFarmTileWith(farmFloodStamps, farmFloodQueue, farmFloodGeneration, x, y, resourceType, teamMask, entry, availableOwner);
}
std::optional<size_t> Map::pickFarmHarvestTileSlotIn(FarmFloodScratch& scratch, int x, int y, int resourceType, Uint32 teamMask, std::optional<size_t> entry, std::optional<ResourceClaims::Owner> availableOwner) const
{
	return pickFarmTileWith(scratch.stamps, scratch.queue, scratch.generation, x, y, resourceType, teamMask, entry, availableOwner);
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
    const auto& properties=resourcePropertiesByIndex(r.type);
    const bool oscillates=properties.growthRate && properties.ecology!=ResourceEcology::None;
    bool changed=false;
    for (unsigned remaining=properties.materialMask;remaining;remaining&=remaining-1)
    {
        const auto m=std::countr_zero(remaining);
        const auto& y=yields[m];
        if (!y.growthRate) continue;
        const auto amount=materialAmountAtSlot(index,m);
        if (amount>=y.capacity && (!oscillates || y.capacity==1)) continue;
        const auto increment=Fertility::growthOpportunities(y.growthRate,[&]{return privateRandom(RandomDomain::ResourceStocks).nextU32();});
        if (increment)
        {
            // Mature deposits breathe between capacity and one unit below it.
            setMaterialAmountSlot(index,m,amount>=y.capacity ? amount-1 :
                std::min<unsigned>(y.capacity,unsigned(amount)+increment));
            changed=true;
        }
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
                    r.amount+=privateRandom(RandomDomain::ResourcePlacement).nextU32()%(yld.placementMaximum-yld.initial+1);
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
	const auto i = coordToIndex(x,y);
	const auto value = Uint16(scriptAreaCells[i] | (1<<n));
	if (value != scriptAreaCells[i]) { scriptAreaCells[i] = value; markArea(i); }
}

void Map::unsetPoint(int n, int x, int y)
{
	const auto i = coordToIndex(x,y);
	const auto value = Uint16(scriptAreaCells[i] & ~(1<<n));
	if (value != scriptAreaCells[i]) { scriptAreaCells[i] = value; markArea(i); }
}

std::string Map::getAreaName(int n) const
{
	return areaNames[n];
}

void Map::setAreaName(int n, std::string name)
{
	if (areaNames[n] != name) { areaNames[n] = std::move(name); areaChanges.markAll(); }
}


RouteResult MapRouting::queryMaterialDistance(int team, int material, MovementAccess movementAccessValue, int x, int y, int* distance, bool markets, const Building* consumer)
{
	const auto* field=getMaterialGradientSlot(team,material,movementAccessValue,markets,consumer);
	if (!field) return RouteResult::Pending;
	const auto value=field[map.coordToIndex(x,y)];
    const auto key=materialGradientFieldKey(team,material,movementAccessValue,field);
    if(key && gradientRuntime->qualityProbe && map.game && !map.compute.insideJob())
        gradientRuntime->qualityProbe->observePlanning(map,*key,x,y,field);
    if(value<=GRADIENT_UNREACHABLE) {
        // Failed recruitment needs a recovery path even without a moving unit.
        // Successful planning reads and both outcomes' movement counters stay idle.
        if(key && map.game && !map.compute.insideJob())gradientRuntime->adaptive.enroll(*key).demand=true;
        return RouteResult::Unreachable;
    }

	if (distance) *distance=gradientTiles(value);
	return RouteResult::Reachable;
}

bool MapRouting::materialAvailableSlot(int team, int material, MovementAccess movementAccessValue, int x, int y, bool markets, const Building* consumer)
{
	return queryMaterialDistance(team,material,movementAccessValue,x,y,nullptr,markets,consumer)==RouteResult::Reachable;
}

bool MapRouting::materialAvailableSlot(int team, int material, MovementAccess movementAccessValue, int x, int y, int* distance, bool markets, const Building* consumer)
{
	return queryMaterialDistance(team,material,movementAccessValue,x,y,distance,markets,consumer)==RouteResult::Reachable;
}

bool MapRouting::materialDestinationValidSlot(int team, int material, MovementAccess movementAccessValue, int x, int y, bool markets, const Building* consumer) const
{
    const int movement=accessClass(movementAccessValue);
    if (movement!=AIR_MOVEMENT_CLASS && map.isForbidden(x,y,Team::teamNumberToMask(team))) return false;
    if (map.isMaterialTakeableSlot(x,y,material) && !map.isForbidden(x,y,Team::teamNumberToMask(team))) {
        const auto index=map.coordToIndex(x,y);
        const auto& properties=map.resourcePropertiesByIndex(map.getResource(index).type);
        if(!properties.visibleToHarvest || (map.fogOfWar[index]&Team::teamNumberToMask(team)))return true;
    }
    if (!map.game) return false;
    const unsigned modes=consumer ? materialSupplyModesSlot(consumer,material) : (markets && marketsV2Enabled() ? 1u : 0u);
    if (!modes) return false;
    const Team& owner=*map.game->teams[team];
    const auto eligible=[&](const Building* b) { return stockSupplierEligibleSlot(b,consumer,material,modes); };
    const Uint16 id=map.getBuilding(x,y);
    if (id!=NOGBID && Building::GIDtoTeam(id)==team && eligible(owner.myBuildings[Building::GIDtoID(id)])) return true;
    const auto overlay=[&](const Building* b) {
        return !b->type->semantics.occupiesGround && ((x-b->posX)&map.wMask)<b->type->width
            && ((y-b->posY)&map.hMask)<b->type->height && eligible(b);
    };
    if (modes&1) for (const Building* b:owner.stockSuppliers) if (overlay(b)) return true;
    if (modes&2) for (const Building* b:owner.directStockSuppliers) if (overlay(b)) return true;
    return false;
}

bool MapRouting::projectMaterialDestination(const Uint16* field, int team, int material, MovementAccess movementAccessValue, int x, int y,
    Sint32* targetX, Sint32* targetY, bool markets, const Building* consumer) const
{
    const int movement=accessClass(movementAccessValue);
    if (!field) return false;
    x&=map.wMask; y&=map.hMask;
    Uint16 here=field[map.coordToIndex(x,y)];
    if (here<=GRADIENT_UNREACHABLE) return false;
    if (const auto destinations=materialGradientDestinations(field)) {
        const size_t index=map.coordToIndex(x,y);
        const unsigned dx=destinations->x[index],dy=destinations->y[index];
        if (dx>=unsigned(map.w) || dy>=unsigned(map.h) || !materialDestinationValidSlot(team,material,movementAccessValue,dx,dy,markets,consumer)) return false;
        *targetX=dx; *targetY=dy;
        return true;
    }
    // Match directionByGradient's cost and tie order. The actual first step
    // already accounts for traffic; projection ignores future moving units and
    // never draws random sidesteps. Strict improvement bounds this traversal.
    while (here!=GRADIENT_AT_GOAL) {
        int best=-1, direction=-1;
        for (int d=0;d<8;++d) {
            const int dx=tabClose[d][0], dy=tabClose[d][1];
            const size_t next=map.coordToIndex(x+dx,y+dy);
            if (field[next]<=here) continue;
            const int score=field[next]-stepCost(dx,dy,next,movement);
            if (score>best) { best=score; direction=d; }
        }
        if (direction<0) break;
        x=(x+tabClose[direction][0])&map.wMask;
        y=(y+tabClose[direction][1])&map.hMask;
        here=field[map.coordToIndex(x,y)];
    }
    if (!materialDestinationValidSlot(team,material,movementAccessValue,x,y,markets,consumer)) return false;
    *targetX=x; *targetY=y;
    return true;
}

template<typename T>
bool MapRouting::getGlobalGradientDestination(const T *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const
{
	const T atGoal = std::numeric_limits<T>::max();
	// we start from our current position
	int vx = x & map.wMask;
	int vy = y & map.hMask;
	// max is initialized to gradient value of current position
	T max = gradient[map.coordToIndex(vx, vy)];
	
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
			T g = gradient[map.coordToIndex(vx + ddx, vy + ddy)];
			if (g>max)
			{
				max = g;
				vddx = ddx;
				vddy = ddy;
				found = true;
			}
		}
		
		// change position
		vx = (vx+vddx) & map.wMask;
		vy = (vy+vddy) & map.hMask;
		
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

template bool MapRouting::getGlobalGradientDestination<Uint8>(const Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;
template bool MapRouting::getGlobalGradientDestination<Uint16>(const Uint16 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const;



void Map::prepareClaimContention(int team)
{
    if(activeClaimsEnabled() && game)claimContention.rebuild(*this,team,game->stepCounter);
}

unsigned Map::claimContentionOthers(const Unit& unit,int material,int x,int y)
{
    const int team=unit.owner->teamNumber;
    const Uint64 tick=game?game->stepCounter:0;
    if(!claimContention.current(team,tick))claimContention.rebuild(*this,team,tick);
    return claimContention.others(*this,unit,material,x,y,ResourceClaims::Policy::ContentionRadius);
}

ResourceClaims::ClaimTerms Map::claimTerms(Uint16 resource,int material,Uint32 access,Uint32 teamMask) const
{
    const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(resource))[material];
    ResourceClaims::ClaimTerms terms;
    terms.exclusive=yield.consumption==ResourceConsumption::All || yield.destroysDeposit;
    terms.pooled=!terms.exclusive && farmAreasEnabled() && (areaCells[access].farm&teamMask) && isFarmableResourceByIndex(resource);
    terms.infinite=yield.consumption==ResourceConsumption::Infinite;
    terms.reserve=terms.pooled?pooledClaimReserve(yield):0;
    return terms;
}

Uint16 Map::pooledClaimReserve(const YieldProperties& yield) const
{
    // Infinite yields never deplete; without regrowth every unit is spendable.
    if(yield.consumption==ResourceConsumption::Infinite || (game && game->gameHeader.isResourceGrowthDisabled()))return 0;
    return yield.seedReserve;
}

void Map::enableResourceClaims()
{
    // A map exported from a running match keeps its tick but carries no claim
    // or route state, so loading it is still new-game setup.
    routing().setMaterialDestinationsEnabled(true);
    if(game)for(int team=0;team<game->teamsCount();++team)for(Unit* unit:game->teams[team]->liveUnits) {
        unit->releaseActiveMaterialClaim();unit->materialClaimTask=0;unit->materialClaimPolicyState={};
    }
    materialClaimRoutes.reset();activeClaimRules.reset();
    resourceClaimsEnabled=true;claimSetupTick=game?game->stepCounter:0;
    materialClaims.reset(cellCount());checkActiveClaimRules();
    if(game)game->invalidateObservation();
}

void Map::disableResourceClaims()
{
    // Only before the first simulated tick: a game started from a map exported
    // mid-match begins at a nonzero tick, so compare with the setup tick.
    if(game && resourceClaimsEnabled && claimSetupTick!=std::optional<Uint64>(game->stepCounter))
        throw std::logic_error("resource claims can only be disabled at new-game setup");
    if(game)for(int team=0;team<game->teamsCount();++team)for(Unit* unit:game->teams[team]->liveUnits) {
        unit->releaseActiveMaterialClaim();unit->materialClaimTask=0;unit->materialClaimPolicyState={};
    }
    materialClaimRoutes.reset();activeClaimRules.reset();
    resourceClaimsEnabled=false;claimSetupTick.reset();
    materialClaims.reset(cellCount());
    routing().setMaterialDestinationsEnabled(false);
    if(game)game->invalidateObservation();
}

// Validates every unit's claim and claim route against the current world and
// rebuilds `ledger` from them, in unit order. Load runs it into the live
// ledger; the save-time audit runs it into a scratch ledger and compares, so an
// invariant hole fails where it is created rather than as an unloadable save.
void Map::validateActiveMaterialClaims(ResourceClaims::Ledger& ledger) const
{
    const std::array<Uint64,3> currentRules{Uint64(farmAreasEnabled()),Uint64(game && game->gameHeader.isResourceGrowthDisabled()),resourceRegistry().checksum()};
    const bool rulesMismatch=activeClaimRules && *activeClaimRules!=currentRules;
    FarmFloodScratch scratch;
    ledger.reset(cellCount());
    for(int team=0;team<game->teamsCount();++team)for(Unit* unit:game->teams[team]->liveUnits) {
        if(!activeClaimsEnabled() && (unit->activeMaterialClaim || unit->materialClaimTask))
            throw std::runtime_error("disabled active claims contain unit state");
        const auto& cadence=unit->materialClaimPolicyState;
        if(!activeClaimsEnabled() && cadence!=ResourceClaims::MaterialClaimPolicyState{})
            throw std::runtime_error("disabled claims contain policy state");
        if(cadence.lastGoalAttempt>cadence.decisionOrdinal)throw std::runtime_error("invalid saved material claim policy state");
        if(unit->activeMaterialClaim) {
            const auto& c=*unit->activeMaterialClaim;
            const auto owner=ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity);
            if(c.source>=cellCount() || c.access>=cellCount() || !validMaterial(c.material) || !unit->materialClaimTask)
                throw std::runtime_error("invalid saved active claim structure");
            if(rulesMismatch) {
                ledger.record(owner,c);
                continue; // Preserve the scheduled rule-change invalidation for next publication.
            }
            if(!resourceRegistry().valid(static_cast<ResourceId>(c.resource))
                || !unit->activeMaterialClaimValid() || !activeClaimFarmConnectedIn(scratch,c,unit->owner->me))
                throw std::runtime_error("invalid saved active claim ownership");
            const auto terms=claimTerms(c.resource,c.material,c.access,unit->owner->me);
            const auto amount=materialAmountAtSlot(c.source,c.material);
            if(c.exclusive!=terms.exclusive || c.reserve!=terms.reserve
                || !ledger.acquire(owner,c,amount>terms.reserve?amount-terms.reserve:0,terms.infinite))
                throw std::runtime_error("saved active claims exceed source capacity");
        }
    }
    if(materialClaimRoutes)for(const auto& [owner,state]:materialClaimRoutes->states) {
        const auto gid=Uint16(owner);const auto team=Unit::GIDtoTeam(gid),slot=Unit::GIDtoID(gid);
        if(team>=game->teamsCount() || !game->teams[team])throw std::runtime_error("invalid saved claim route owner");
        const auto* unit=game->teams[team]->myUnits[slot];
        if(!unit || ResourceClaims::unitOwner(gid,unit->scriptIdentity)!=owner || !unit->activeMaterialClaim
            || unit->materialClaimTask!=state.identity.task || unit->activeMaterialClaim->access!=state.target)
            throw std::runtime_error("saved claim route does not match unit claim");
    }
}

void Map::rebuildActiveMaterialClaims()
{
    validateActiveMaterialClaims(materialClaims);
}

void Map::auditActiveMaterialClaims() const
{
    if(!game)return;
    ResourceClaims::Ledger expected;
    validateActiveMaterialClaims(expected);
    if(expected.entries()!=materialClaims.entries())
        throw std::logic_error("resource claim ledger differs from the units' claims");
}

bool Map::activeClaimFarmConnected(const ResourceClaims::Claim& claim,Uint32 teamMask)
{
    if(claim.source==claim.access)return true;
    if(claim.source>=cellCount() || claim.access>=cellCount() || !farmAreasEnabled())return false;
    pickFarmHarvestTileSlot(int(claim.access%getW()),int(claim.access/getW()),claim.material,teamMask,claim.access);
    return farmFloodStamps.size()==cellCount() && farmFloodStamps[claim.source]==farmFloodGeneration;
}

bool Map::activeClaimFarmConnectedIn(FarmFloodScratch& scratch,const ResourceClaims::Claim& claim,Uint32 teamMask) const
{
    if(claim.source==claim.access)return true;
    if(claim.source>=cellCount() || claim.access>=cellCount() || !farmAreasEnabled())return false;
    pickFarmHarvestTileSlotIn(scratch,int(claim.access%getW()),int(claim.access/getW()),claim.material,teamMask,claim.access);
    return scratch.stamps.size()==cellCount() && scratch.stamps[claim.source]==scratch.generation;
}

bool Map::takeClaimedMaterial(const ResourceClaims::Claim& claim,Uint32 teamMask)
{
    if(claim.source>=cellCount() || claim.access>=cellCount() || !validMaterial(claim.material)
        || getResource(claim.source).type!=claim.resource || !materialAmountAtSlot(claim.source,claim.material)
        || isForbidden(int(claim.access%getW()),int(claim.access/getW()),teamMask)
        || !activeClaimFarmConnected(claim,teamMask))return false;
    const auto& yield=resourceRegistry().yields(static_cast<ResourceId>(claim.resource))[claim.material];
    if(yield.consumption!=ResourceConsumption::Infinite && materialAmountAtSlot(claim.source,claim.material)<=claim.reserve)return false;
    return harvestMaterial(claim.source,claim.material);
}
