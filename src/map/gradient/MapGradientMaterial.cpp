// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include <bit>
#include "gradient/GradientRuntime.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"
#include "MapInternal.h"

#include <mutex>
#include <array>
#include "SeedCells.h"
#include <type_traits>

namespace {
// The pipeline borrows the address of published, not just its array. Eviction
// must retain the whole stable entry until every logical ticket has retired.
// First-read holds depend only on simulation ticks, never worker completion.
bool retainedMaterialField(const GradientRuntime& runtime, const GradientRuntime::MaterialField& field, const Game* game)
{
    bool retained=false;
    for (const auto& queued:runtime.fieldRequests) if (queued.slot==&field.published) retained=true;
    runtime.pipeline.visitLogical([&](auto* slot,unsigned,bool) { if (slot==&field.published) retained=true; });
    if (!retained && field.awaitingFirstRead && field.published
        && Uint32(game->stepCounter-field.publishedStep)<GradientRuntime::MaterialReadHoldTicks) {
        const auto* consumer=field.consumer<0 ? nullptr : game->teams[field.team]->myBuildings[Building::GIDtoID(field.consumer)];
        retained=field.consumer<0 || (consumer && consumer->scriptIdentity==field.identity && consumer->typeNum==field.type
            && consumer->posX==field.x && consumer->posY==field.y && consumer->usesTeamMaterials()==field.excludesShared);
    }
    return retained;
}
}

const Uint16 *Map::getMaterialGradientSlot(int teamNumber, int resourceType, int swimClass, bool withMarkets, const Building* consumer)
{
	if (consumer)
	{
		const unsigned modes=materialSupplyModesSlot(consumer,resourceType);
		withMarkets=modes&1;
		const unsigned supplied=((modes&1) ? consumer->runtime->suppliesStockMask : 0)
			| ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0);
		const bool excludesSelf=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) || (supplied&(1u<<resourceType));
		if ((modes&2) || (withMarkets && excludesSelf)) return cachedMaterialGradientSlot(consumer,resourceType,swimClass,modes);
	}
	withMarkets = withMarkets && marketsV2Enabled();
	// Keep colonies without markets on the original field and refresh schedule.
	if (withMarkets && game->teams[teamNumber]->stockSuppliers.empty()) withMarkets=false;
	// Workers may observe a published field but cannot enroll shared requests.
	// The owner alone changes destinations and publishes at pipeline boundaries.
	std::lock_guard<std::mutex> lock(materialGradientMutex);
    // Source counts prove this answer without a route build. Preserve the
    // shared negative field: missing natural materials must not enroll one
    // full-map job per team/swim class on every refresh round. Supplier-backed
    // and consumer-specific fields above still require their normal snapshots.
    if (!withMarkets && !hasMaterialSourceSlot(resourceType)) {
        auto& empty=gradientRuntime->absentMaterialField;
        if (empty.size()!=size) empty.assign(size,GRADIENT_UNREACHABLE);
        return empty.data();
    }
    Uint16 *&gradient = withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
    if (!gradient && !compute.insideJob() && (!game || swimClass==AIR_MOVEMENT_CLASS)) { gradient=new Uint16[size]; updateMaterialGradient(teamNumber,resourceType,swimClass,withMarkets); }
    // Preserve deferred cold allocation. Suspended ground fields also re-enroll
    // on explicit demand; the serving snapshot stays usable until publication.
    const bool stale=swimClass<SWIM_CLASS_COUNT && (withMarkets
        ? marketMaterialGradientStale[teamNumber][resourceType][swimClass]
        : materialGradientStale[teamNumber][resourceType][swimClass]);
    if ((!gradient || stale) && !compute.insideJob())
        requestGradientField(&gradient, int(withMarkets ? gradient_preparation::Kind::Markets : gradient_preparation::Kind::Materials), teamNumber, resourceType, swimClass);
	return gradient;
}

void Map::updateMaterialGradient(int teamNumber, Uint8 resourceType, int swimClass, bool withMarkets)
{
    if (game && swimClass!=AIR_MOVEMENT_CLASS) {
        auto& field=withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
        requestGradientField(&field, int(withMarkets ? gradient_preparation::Kind::Markets : gradient_preparation::Kind::Materials), teamNumber, resourceType, swimClass); return;
    }

	withMarkets = withMarkets && marketsV2Enabled();
	PERF_SCOPE_TIME(ResourceGradient);
	auto &slot = withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
	gradientRuntime->pipeline.invalidate(&slot);
	Uint16 *gradient = slot;
	if (!withMarkets && swimClass<SWIM_CLASS_COUNT) {
        gradientRuntime->initializeResourcePolicy(*this);
        auto& field=gradientRuntime->resources.fields[(teamNumber*MaterialCount+resourceType)*SWIM_CLASS_COUNT+swimClass];
        field.serving=field.capture(game ? game->stepCounter : 0,gradientRuntime->resources.sources[resourceType]);
        field.submitted=field.serving.tick;
        if (auto& probe=gradientRuntime->qualityProbe) {
            const int key=(teamNumber*MaterialCount+resourceType)*SWIM_CLASS_COUNT+swimClass;
            probe->capturedMovement(key,field.serving.tick);
            probe->publishedMovement(key,field.serving.tick);
        }
    }
	seedMaterialGradient(teamNumber, resourceType, swimClass, gradient, withMarkets);
	{
		PERF_SCOPE_TIME(PropagationResource);
		propagateGradient(gradient, swimClass, GRADIENT_COST_LIMIT,
            withMarkets ? gradient_kernel::Family::Markets : gradient_kernel::Family::Materials);
	}
    (withMarkets ? marketMaterialGradientStale[teamNumber][resourceType][swimClass]
                 : materialGradientStale[teamNumber][resourceType][swimClass])=false;
	publishPlane(&slot);
	if (withMarkets) marketGradientDirty[teamNumber][resourceType][swimClass]=false;
}

void Map::seedMaterialGradient(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, bool withMarkets, const Building* consumer, unsigned modes)
{
	assert(gradient);
	if (modes || (withMarkets && marketsV2Enabled()))
		return seedMaterialGradientWithSuppliers(teamNumber, resourceType, swimClass, gradient, consumer, modes ? modes : 1);
	// Keep supplier storage and traversal out of natural-only preparation,
	// including the frequent warmed-cache path.
	if (!gradientRuntime->resourceSeeds.trySeed(*this, teamNumber, resourceType, swimClass, gradient, nullptr))
		seedMaterialGradientDirect(teamNumber, resourceType, swimClass, gradient, nullptr);
}

void Map::seedMaterialGradientWithSuppliers(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, const Building* consumer, unsigned modes)
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
		if (stockSupplierEligibleSlot(supplier,consumer,resourceType,modes))
			supplierSeeds[Building::GIDtoID(supplier->gid)] = std::max<int>(GRADIENT_UNREACHABLE + 1,
				GRADIENT_AT_GOAL - supplier->type->semantics.market.pickupPenalty * GRADIENT_STEP);
	});
	if (!gradientRuntime->resourceSeeds.trySeed(*this, teamNumber, resourceType, swimClass, gradient, supplierSeeds.data()))
		seedMaterialGradientDirect(teamNumber, resourceType, swimClass, gradient, supplierSeeds.data());
	// Overlay providers have no tile occupancy entry. Seed their small
	// footprints after either cached or direct preparation.
	if ((modes&2) || game->buildingsTypes.usesOverlaySuppliers())
	{
		const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
		const auto *occupancy = occupancyCells.data();
		const auto *areas = areaCells.data();

		visitSuppliers([&](const Building* supplier) {
			const Uint16 seed = supplierSeeds[Building::GIDtoID(supplier->gid)];
			if (supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround) || seed <= GRADIENT_UNREACHABLE) return;
			for (int y=0; y<supplier->type->height; ++y)
				for (int x=0; x<supplier->type->width; ++x)
				{
					const size_t i = coordToIndex(supplier->posX+x, supplier->posY+y);
					if (swimClass==AIR_MOVEMENT_CLASS || (!(areas[i].forbidden & teamMask) && occupancy[i].immobileUnit == IMMOBILE_UNIT_NONE))
						gradient[i] = std::max(gradient[i], seed);
				}
		});
	}
}

void Map::seedMaterialGradientDirect(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, const Uint16 *supplierSeeds)
{
    gradient_preparation::materialCells(liveCells, fogOfWar, teamNumber, resourceType, swimClass, gradient, supplierSeeds,
        [this](auto fn) { initializeGradientCells(fn); });
}

void Map::dirtyMarketGradientsSlot(int teamNumber, int resourceType)
{
	if (resourceType<MaterialCount) ++gradientRuntime->stockRevision[teamNumber][resourceType];
	if (!marketsV2Enabled()) return;
	for (int s=0; s<MOVEMENT_CLASS_COUNT; ++s)
		marketGradientDirty[teamNumber][resourceType][s]=true;

}


bool Map::stockSupplierEligibleSlot(const Building* supplier, const Building* consumer, int resource, unsigned modes) const
{
    return supplier && supplier!=consumer && (!consumer || !supplier->sharesMaterialPool(*consumer))
        && supplier->buildingState==Building::ALIVE
        && ((((modes&1) ? supplier->runtime->suppliesStockMask : 0) | ((modes&2) ? supplier->runtime->suppliesDirectStockMask : 0))&(1u<<resource))
        && supplier->availableMaterial(resource)>0;
}

const Uint16* Map::cachedMaterialGradientSlot(const Building* consumer, int resource, int swim, unsigned modes)
{
    std::lock_guard<std::mutex> lock(materialGradientMutex);
    auto& runtime=*gradientRuntime;
    const int team=consumer->owner->teamNumber;
    const bool privateField=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) ||
        ((((modes&1) ? consumer->runtime->suppliesStockMask : 0) | ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0))&(1u<<resource));
    const int excluded=privateField ? consumer->gid : -1;
    const Uint64 key=MapState::materialFieldKey(excluded,team,resource,swim,modes);
    auto found=runtime.materialFields.find(key);
    if (compute.insideJob()) {
        if (found==runtime.materialFields.end()) return nullptr;
        const auto& entry=found->second;
        if (privateField && (entry.identity!=consumer->scriptIdentity || entry.type!=consumer->typeNum
            || entry.x!=consumer->posX || entry.y!=consumer->posY || entry.excludesShared!=consumer->usesTeamMaterials())) return nullptr;
        return entry.published;
    }
    if (found==runtime.materialFields.end())
    {
        const Uint64 fieldBytes=Uint64(size)*sizeof(Uint16);
        const Uint64 limit=std::max(fieldBytes,runtime.materialCacheBudget);
        while (!runtime.materialLru.empty() && (runtime.materialFields.size()+1)*fieldBytes>limit)
        {
            auto victim=runtime.materialLru.end();
            for (auto candidate=runtime.materialLru.begin();candidate!=runtime.materialLru.end();++candidate) {
                auto& old=runtime.materialFields.at(*candidate);
                if (!retainedMaterialField(runtime,old,game)) { victim=candidate; break; }
            }
            if (victim==runtime.materialLru.end()) return nullptr;
            runtime.materialFields.erase(*victim); runtime.materialLru.erase(victim);
        }
        found=runtime.materialFields.try_emplace(key).first;
        auto& entry=found->second;

        entry.consumer=excluded; entry.team=team; entry.resource=resource; entry.swim=swim; entry.modes=modes;
        runtime.materialLru.push_back(key); entry.lru=std::prev(runtime.materialLru.end());
    }
    auto& entry=found->second;
    const bool changedConsumer=privateField && (entry.identity!=consumer->scriptIdentity || entry.type!=consumer->typeNum
        || entry.x!=consumer->posX || entry.y!=consumer->posY || entry.excludesShared!=consumer->usesTeamMaterials());
    if (changedConsumer) {
        runtime.pipeline.invalidate(&entry.published);
        std::erase_if(runtime.fieldRequests,[&](const auto& request) { return request.slot==&entry.published; });
        entry.cells.reset(); entry.published=nullptr; entry.awaitingFirstRead=false;
    }
    // Stock and topology changes request a delayed refresh; natural growth and
    // harvest also refresh at the same fixed age on every platform.
    if (!entry.published || entry.recency==0 || changedConsumer || entry.sourceRevision!=runtime.stockRevision[team][resource]
        || entry.topology!=topologyGeneration || Uint32(game->stepCounter-entry.builtStep)>=128)
    {
        bool pending=false;
        for (const auto& queued:runtime.fieldRequests) if (queued.slot==&entry.published) pending=true;
        runtime.pipeline.forEachDestination([&](auto* slot) { if (slot==&entry.published) pending=true; });
        if (swim==AIR_MOVEMENT_CLASS) {
            if (!entry.cells) entry.cells=std::make_unique<Uint16[]>(size);
            seedMaterialGradient(team,resource,swim,entry.cells.get(),true,privateField ? consumer : nullptr,modes);
            propagateGradient(entry.cells.get(),swim,GRADIENT_COST_LIMIT,gradient_kernel::Family::Markets);
            entry.published=entry.cells.get();
        } else if (!compute.insideJob()) requestGradientField(&entry.published, int(gradient_preparation::Kind::Markets),team,resource,swim,excluded,modes);
        // Keep the revision of the admitted request. Changes observed while its
        // ticket is pending must remain dirty after that result publishes.
        if (!pending) {
            entry.sourceRevision=runtime.stockRevision[team][resource]; entry.topology=topologyGeneration;
            entry.builtStep=game->stepCounter; entry.identity=privateField ? consumer->scriptIdentity : 0;
            entry.excludesShared=privateField && consumer->usesTeamMaterials();
            entry.type=privateField ? consumer->typeNum : -1;
            entry.x=privateField ? consumer->posX : 0; entry.y=privateField ? consumer->posY : 0;
        }
    }
    entry.recency=++runtime.materialCacheClock;
    runtime.materialLru.splice(runtime.materialLru.end(),runtime.materialLru,entry.lru);
    if (entry.published) entry.awaitingFirstRead=false;
    return entry.published;
}
void Map::setMaterialRoutingCacheBudget(Uint64 bytes)
{
    auto& runtime=*gradientRuntime;
    const auto target=std::clamp<Uint64>(bytes,sizeof(Uint16)*size,std::max<Uint64>(sizeof(Uint16)*size,64ull*1024*1024));
    while (materialRoutingCacheBytes()>target) {
        auto victim=runtime.materialLru.end();
        for (auto it=runtime.materialLru.begin();it!=runtime.materialLru.end();++it) {
            auto& entry=runtime.materialFields.at(*it);
            if (!retainedMaterialField(runtime,entry,game)) { victim=it; break; }
        }
        if (victim==runtime.materialLru.end()) break;
        runtime.materialFields.erase(*victim); runtime.materialLru.erase(victim);
    }
    // Configuration cannot destroy an admitted destination. A shrink takes the
    // smallest budget that still retains its logically outstanding tickets.
    runtime.materialCacheBudget=std::max(target,materialRoutingCacheBytes());
}
Uint64 Map::materialRoutingCacheBytes() const
{
    return Uint64(gradientRuntime->materialFields.size())*size*sizeof(Uint16);
}

unsigned Map::materialSupplyModesSlot(const Building* consumer, int resource) const
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

std::optional<Uint16> Map::planeKeyForSlot(Uint16* const* slot) const
{
	constexpr auto perArray = std::size_t(Team::MAX_COUNT) * MaterialSlotCount * MOVEMENT_CLASS_COUNT;
	const auto decode = [&](Uint16* const* first, bool market) {
		const auto offset = std::size_t(slot - first);
		return MapState::planeKey(int(offset / (MaterialSlotCount * MOVEMENT_CLASS_COUNT)),
			int((offset / MOVEMENT_CLASS_COUNT) % MaterialSlotCount), int(offset % MOVEMENT_CLASS_COUNT), market);
	};
	if (const auto* first = &materialGradients[0][0][0]; slot >= first && slot < first + perArray) return decode(first, false);
	if (const auto* first = &marketMaterialGradients[0][0][0]; slot >= first && slot < first + perArray) return decode(first, true);
	return std::nullopt;
}

void Map::publishPlane(Uint16* const* slot)
{
	const auto found = planeKeyForSlot(slot);
	if (!found) return;
	const auto key = *found;
	auto& index = publishedPlaneIndex[key];
	if (!index)
	{
		publishedPlanes.push_back({key, 0, slot});
		index = Uint16(publishedPlanes.size());
	}
	publishedPlanes[index - 1].generation = ++planeGenerations[key];
}

void Map::rebuildPlaneRegistry()
{
	publishedPlanes.clear();
	publishedPlaneIndex.fill(0);
	for (int team = 0; team < Team::MAX_COUNT; ++team)
		for (int resource = 0; resource < MaterialSlotCount; ++resource)
			for (int swim = 0; swim < MOVEMENT_CLASS_COUNT; ++swim)
			{
				if (materialGradients[team][resource][swim]) publishPlane(&materialGradients[team][resource][swim]);
				if (marketMaterialGradients[team][resource][swim]) publishPlane(&marketMaterialGradients[team][resource][swim]);
			}
}

const Uint16* Map::lookupMaterialGradientSlot(int team, int material, int swim, bool markets, const Building* consumer) const
{
    if (consumer) {
        const auto modes=materialSupplyModesSlot(consumer,material);
        markets=modes&1;
        const unsigned supplied=((modes&1)?consumer->runtime->suppliesStockMask:0)
            | ((modes&2)?consumer->runtime->suppliesDirectStockMask:0);
        const bool privateField=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) || (supplied&(1u<<material));
        if ((modes&2) || (markets && privateField)) {
            const auto key=MapState::materialFieldKey(privateField?consumer->gid:-1,team,material,swim,modes);
            const auto it=gradientRuntime->materialFields.find(key);
            if (it==gradientRuntime->materialFields.end()) return nullptr;
            const auto& entry=it->second;
            if (privateField && (entry.identity!=consumer->scriptIdentity || entry.type!=consumer->typeNum || entry.x!=consumer->posX || entry.y!=consumer->posY || entry.excludesShared!=consumer->usesTeamMaterials())) return nullptr;
            return entry.published;
        }
    }
    markets=markets && marketsV2Enabled() && !game->teams[team]->stockSuppliers.empty();
    return markets ? marketMaterialGradients[team][material][swim] : materialGradients[team][material][swim];
}

void GradientRuntime::initializeResourcePolicy(const Map& map)
{
    if (resources.initialized) return;
    resources.sources={};
    for (size_t i=0;i<size_t(map.getW())*map.getH();++i)
        for (unsigned bits=map.materialMaskAt(i);bits;bits&=bits-1)
            ++resources.sources[std::countr_zero(bits)].count;
    resources.initialized=true;
}
