// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"
#include "MapInternal.h"

#include <mutex>
#include <array>
#include "SeedTerrain.h"
#include <type_traits>

Uint16 *Map::getResourceGradient(int teamNumber, int resourceType, int swimClass, bool withMarkets, const Building* consumer)
{
	if (consumer)
	{
		const unsigned modes=resourceSupplyModes(consumer,resourceType);
		withMarkets=modes&1;
		const unsigned supplied=((modes&1) ? consumer->runtime->suppliesStockMask : 0)
			| ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0);
		const bool excludesSelf=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) || (supplied&(1u<<resourceType));
		if ((modes&2) || (withMarkets && excludesSelf)) return cachedResourceGradient(consumer,resourceType,swimClass,modes);
	}
	withMarkets = withMarkets && marketsV2Enabled();
	// Keep colonies without markets on the original field and refresh schedule.
	if (withMarkets && game->teams[teamNumber]->stockSuppliers.empty()) withMarkets=false;
	// AI workers may request the same lazy field concurrently. Cover both
	// allocation and pipeline invalidation before publishing the pointer.
	std::lock_guard<std::mutex> lock(resourcesGradientMutex);
	Uint16 *&gradient = withMarkets ? marketResourcesGradient[teamNumber][resourceType][swimClass] : resourcesGradient[teamNumber][resourceType][swimClass];
	if (gradient == NULL)
	{
		gradient = new Uint16[size];
		updateResourcesGradient(teamNumber, resourceType, swimClass, withMarkets);
	}
	return gradient;
}

void Map::updateResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	PERF_SCOPE_TIME(ResourceGradient);
	auto &slot = withMarkets ? marketResourcesGradient[teamNumber][resourceType][swimClass] : resourcesGradient[teamNumber][resourceType][swimClass];
	gradientRuntime->pipeline.invalidate(&slot);
	Uint16 *gradient = slot;
	seedResourcesGradient(teamNumber, resourceType, swimClass, gradient, withMarkets);
	propagateGradient(gradient, swimClass);
	++resourceFieldGenerations[&slot];
	if (withMarkets) marketGradientDirty[teamNumber][resourceType][swimClass]=false;
}

void Map::seedResourcesGradient(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, bool withMarkets, const Building* consumer, unsigned modes)
{
	assert(gradient);
	if (modes || (withMarkets && marketsV2Enabled()))
		return seedResourcesGradientWithSuppliers(teamNumber, resourceType, swimClass, gradient, consumer, modes ? modes : 1);
	// Keep supplier storage and traversal out of natural-only preparation,
	// including the frequent warmed-cache path.
	if (!gradientRuntime->resourceSeeds.trySeed(*this, teamNumber, resourceType, swimClass, gradient, nullptr))
		seedResourcesGradientDirect(teamNumber, resourceType, swimClass, gradient, nullptr);
}

void Map::seedResourcesGradientWithSuppliers(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, const Building* consumer, unsigned modes, bool useCache)
{
	// Compile supplier eligibility and cost once; the map-sized loop only reads
	// compact instance-indexed values, never the building catalog.
	std::array<Uint16, Building::MAX_COUNT> supplierSeeds;
	const Team& team=*game->teams[teamNumber];
	const auto visitSuppliers=[&](auto visit) {
		if (modes&1) for (const Building* supplier : team.stockSuppliers) visit(supplier);
		if (modes&2) for (const Building* supplier : team.directStockSuppliers)
			if (!(modes&1) || !(supplier->runtime->suppliesStockMask&(1u<<resourceType))) visit(supplier);
	};
	supplierSeeds.fill(GRADIENT_FORBIDDEN);
	visitSuppliers([&](const Building* supplier) {
		if (stockSupplierEligible(supplier,consumer,resourceType,modes))
			supplierSeeds[Building::GIDtoID(supplier->gid)] = std::max<int>(GRADIENT_UNREACHABLE + 1,
				GRADIENT_AT_GOAL - supplier->type->semantics.market.pickupPenalty * GRADIENT_STEP);
	});
	if (!useCache || !gradientRuntime->resourceSeeds.trySeed(*this, teamNumber, resourceType, swimClass, gradient, supplierSeeds.data()))
		seedResourcesGradientDirect(teamNumber, resourceType, swimClass, gradient, supplierSeeds.data());
	// Overlay providers have no tile occupancy entry. Seed their small
	// footprints after either cached or direct preparation.
	if ((modes&2) || game->buildingsTypes.usesOverlaySuppliers())
	{
		const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
		const Tile *tile = tiles.data();
		const Uint8 *immobile = immobileUnits;
		visitSuppliers([&](const Building* supplier) {
			const Uint16 seed = supplierSeeds[Building::GIDtoID(supplier->gid)];
			if (supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround) || seed <= GRADIENT_UNREACHABLE) return;
			for (int y=0; y<supplier->type->height; ++y)
				for (int x=0; x<supplier->type->width; ++x)
				{
					const size_t i = coordToIndex(supplier->posX+x, supplier->posY+y);
					if (!(tile[i].forbidden & teamMask) && immobile[i] == IMMOBILE_UNIT_NONE)
						gradient[i] = std::max(gradient[i], seed);
				}
		});
	}
}

void Map::seedResourcesGradientDirect(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, const Uint16 *supplierSeeds)
{
	const bool withMarkets = supplierSeeds != nullptr;
	assert(gradient);
	const bool canSwim = swimClass > 0;

	const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	const unsigned teamBuildingBase=unsigned(teamNumber)*Building::MAX_COUNT;
	assert(globalContainer);
	// Only fogged resources of a type that must be seen to be collected are hidden.
	const bool hideFogged = globalContainer->resourcesTypes.get(resourceType)->visibleToBeCollected;
	const Tile *tile = tiles.data();
	const Uint8 *immobile = immobileUnits;
	const Uint32 *fog = fogOfWar;
	gradient_preparation::withTerrain(*this, canSwim, [&](auto terrainAt) {
		// Compile-time tags select four kernels, removing market and visibility
		// policy branches from the cell loop. Initialization joins before returning.
		auto seed = [&](auto marketsTag, auto hideFoggedTag) {
			initializeGradientCells([&](size_t begin, size_t end) {
				for (size_t i = begin; i < end; ++i)
				{
					const Tile &c = tile[i];
					Uint16 value = GRADIENT_FORBIDDEN;
					if (!(c.forbidden & teamMask) && immobile[i] == IMMOBILE_UNIT_NONE)
					{
						if (c.resource.type == NO_RES_TYPE)
						{
							if (c.building == NOGBID)
								value = terrainAt(i).open;
							else if constexpr (decltype(marketsTag)::value)
							{
								// GIDs are contiguous per team. Unsigned subtraction
								// rejects foreign and invalid IDs before indexing.
								const unsigned localId=unsigned(c.building)-teamBuildingBase;
								if (localId<Building::MAX_COUNT) value=supplierSeeds[localId];
							}
						}
						else if (c.resource.type == resourceType)
						{
							// Resource goals override terrain/buildings, but not
							// the forbidden/immobile blockers checked above.
							value = GRADIENT_AT_GOAL;
							if constexpr (decltype(hideFoggedTag)::value)
								if (!(fog[i] & teamMask)) value = GRADIENT_FORBIDDEN;
						}
					}
					gradient[i] = value;
				}
			});
		};
		if (withMarkets)
		{
			if (hideFogged) seed(std::true_type{}, std::true_type{});
			else seed(std::true_type{}, std::false_type{});
		}
		else
		{
			if (hideFogged) seed(std::false_type{}, std::true_type{});
			else seed(std::false_type{}, std::false_type{});
		}
	});
}

void Map::dirtyMarketGradients(int teamNumber, int resourceType)
{
	if (resourceType<MAX_RESOURCES) ++gradientRuntime->stockRevision[teamNumber][resourceType];
	if (!marketsV2Enabled()) return;
	for (int s=0; s<SWIM_CLASS_COUNT; ++s)
		{
			marketGradientDirty[teamNumber][resourceType][s]=true;
			gradientRuntime->pipeline.invalidate(&marketResourcesGradient[teamNumber][resourceType][s]);
		}
}


bool Map::stockSupplierEligible(const Building* supplier, const Building* consumer, int resource, unsigned modes) const
{
    return supplier && supplier!=consumer && (!consumer || supplier->resources!=consumer->resources)
        && supplier->buildingState==Building::ALIVE
        && ((((modes&1) ? supplier->runtime->suppliesStockMask : 0) | ((modes&2) ? supplier->runtime->suppliesDirectStockMask : 0))&(1u<<resource))
        && supplier->availableResource(resource)>0;
}

Uint16* Map::cachedResourceGradient(const Building* consumer, int resource, int swim, unsigned modes)
{
    std::lock_guard<std::mutex> lock(resourcesGradientMutex);
    auto& runtime=*gradientRuntime;
    const int team=consumer->owner->teamNumber;
    const bool privateField=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) ||
        ((((modes&1) ? consumer->runtime->suppliesStockMask : 0) | ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0))&(1u<<resource));
    const int excluded=privateField ? consumer->gid : -1;
    const Uint64 key=((((Uint64(excluded+1)*Team::MAX_COUNT+team)*MAX_RESOURCES+resource)*SWIM_CLASS_COUNT+swim)*4)+modes;
    auto found=runtime.resourceFields.find(key);
    if (found==runtime.resourceFields.end())
    {
        const Uint64 fieldBytes=Uint64(size)*sizeof(Uint16);
        const Uint64 limit=std::max(fieldBytes,runtime.resourceCacheBudget);
        while (!runtime.resourceLru.empty() && (runtime.resourceFields.size()+1)*fieldBytes>limit)
        {
            runtime.resourceFields.erase(runtime.resourceLru.front());
            runtime.resourceLru.pop_front();
        }
        found=runtime.resourceFields.try_emplace(key).first;
        auto& entry=found->second;
        entry.cells=std::make_unique<Uint16[]>(size);
        entry.consumer=excluded; entry.team=team; entry.resource=resource; entry.swim=swim; entry.modes=modes;
        runtime.resourceLru.push_back(key); entry.lru=std::prev(runtime.resourceLru.end());
    }
    auto& entry=found->second;
    const bool changedConsumer=privateField && (entry.identity!=consumer->scriptIdentity || entry.type!=consumer->typeNum
        || entry.x!=consumer->posX || entry.y!=consumer->posY);
    // Synchronous bounded fields use the same fixed age across platforms. Stock
    // changes invalidate immediately; natural growth/harvest refreshes by age.
    if (entry.recency==0 || changedConsumer || entry.sourceRevision!=runtime.stockRevision[team][resource]
        || entry.topology!=topologyGeneration || Uint32(game->stepCounter-entry.builtStep)>=128)
    {
        seedResourcesGradient(team,resource,swim,entry.cells.get(),true,privateField ? consumer : nullptr,modes);
        propagateGradient(entry.cells.get(),swim);
        entry.sourceRevision=runtime.stockRevision[team][resource]; entry.topology=topologyGeneration;
        entry.builtStep=game->stepCounter; entry.identity=privateField ? consumer->scriptIdentity : 0;
        entry.type=privateField ? consumer->typeNum : -1; entry.x=privateField ? consumer->posX : 0; entry.y=privateField ? consumer->posY : 0;
    }
    entry.recency=++runtime.resourceCacheClock;
    runtime.resourceLru.splice(runtime.resourceLru.end(),runtime.resourceLru,entry.lru);
    return entry.cells.get();
}
void Map::setResourceRoutingCacheBudget(Uint64 bytes)
{
    gradientRuntime->resourceCacheBudget=std::clamp<Uint64>(bytes,sizeof(Uint16)*size,std::max<Uint64>(sizeof(Uint16)*size,64ull*1024*1024));
    while (!gradientRuntime->resourceLru.empty() && resourceRoutingCacheBytes()>gradientRuntime->resourceCacheBudget)
    {
        gradientRuntime->resourceFields.erase(gradientRuntime->resourceLru.front());
        gradientRuntime->resourceLru.pop_front();
    }
}
Uint64 Map::resourceRoutingCacheBytes() const
{
    return Uint64(gradientRuntime->resourceFields.size())*size*sizeof(Uint16);
}

unsigned Map::resourceSupplyModes(const Building* consumer, int resource) const
{
    if (!consumer) return 1;
    const unsigned bit=1u<<resource;
    const auto& catalog=game->buildingsTypes;
    const Team& team=*consumer->owner;
    // A permission alone does not require a supplier field. Catalog masks rule
    // out impossible resources, and empty instance lists retain the natural
    // field's normal refresh schedule without scanning providers on each query.
    unsigned modes=((consumer->runtime->fetchesStockMask&catalog.stockSupplyMask()&bit)
        && !team.stockSuppliers.empty() ? 1u : 0u)
        | ((consumer->runtime->fetchesDirectStockMask&catalog.directSupplyMask()&bit)
        && !team.directStockSuppliers.empty() ? 2u : 0u);
    // If every Direct provider is already exposed through Unified, its shared
    // field is exactly the union. Resolve this property once per catalog.
    if (modes==3 && !(game->buildingsTypes.extraDirectSupplyMask()&bit)) modes=1;
    return modes;
}

// Enrollment is an owner-side logical delivery effect. Workers supply the field
// computed from their observation, so admitting it never reads a newer world.
void Map::installObservedResourceField(int team, int resource, int swim, std::span<const Uint16> values)
{
	if (team < 0 || team >= Team::MAX_COUNT || resource < 0 || resource >= MAX_NB_RESOURCES ||
		swim < 0 || swim >= SWIM_CLASS_COUNT || values.size() != std::size_t(size))
		throw std::invalid_argument("Invalid observed resource field enrollment");
	auto& slot = resourcesGradient[team][resource][swim];
	if (slot) return;
	auto data = std::make_unique<Uint16[]>(size);
	std::copy(values.begin(), values.end(), data.get());
	slot = data.release();
	++resourceFieldGenerations[&slot];
}
