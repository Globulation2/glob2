// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/MapRouting.h"
#include <bit>
#include "gradient/GradientRuntime.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Building.h"
#include "Game.h"
#include "Team.h"
#include "MapInternal.h"

#include <mutex>
#include <cassert>
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

const Uint16 *MapRouting::getMaterialGradientSlot(int teamNumber, int resourceType, MovementAccess movementAccessValue, bool withMarkets, const Building* consumer, std::optional<GradientFieldKey>* selectedKey)
{
    int swimClass=accessClass(movementAccessValue);

    if(selectedKey)selectedKey->reset();
	if (consumer)
	{
		const unsigned modes=materialSupplyModesSlot(consumer,resourceType);
		withMarkets=modes&1;
		const unsigned supplied=((modes&1) ? consumer->runtime->suppliesStockMask : 0)
			| ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0);
		const bool excludesSelf=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) || (supplied&(1u<<resourceType));
		if ((modes&2) || (withMarkets && excludesSelf)) return cachedMaterialGradientSlot(consumer,resourceType,movementAccessFromLegacy(swimClass),modes);
	}
	withMarkets = withMarkets && marketsV2Enabled();
	// Keep colonies without markets on the original field and refresh schedule.
	if (withMarkets && map.game->teams[teamNumber]->stockSuppliers.empty()) withMarkets=false;
	// Workers may observe a published field but cannot enroll shared requests.
	// The owner alone changes destinations and publishes at pipeline boundaries.
	std::lock_guard<std::mutex> lock(materialGradientMutex);
    // Source counts prove this answer without a route build. Preserve the
    // shared negative field: missing natural materials must not enroll one
    // full-map job per team/swim class on every refresh round. Supplier-backed
    // and consumer-specific fields above still require their normal snapshots.
    if (!withMarkets && !map.hasMaterialSourceSlot(resourceType)) {
        auto& empty=gradientRuntime->absentMaterialField;
        if (empty.size()!=map.size) empty.assign(map.size,GRADIENT_UNREACHABLE);
        return empty.data();
    }
    if(selectedKey)*selectedKey=GradientFieldKey{withMarkets?GradientFieldFamily::Markets:GradientFieldFamily::Materials,Uint8(teamNumber),Uint8(swimClass),Uint8(resourceType)};
    Uint16 *&gradient = withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
    if (!gradient && !map.compute.insideJob() && !map.game) { gradient=new Uint16[map.size]; updateMaterialGradient(teamNumber,resourceType,movementAccessFromLegacy(swimClass),withMarkets); }
    // Preserve deferred cold allocation. Suspended ground fields also re-enroll
    // on explicit demand; the serving snapshot stays usable until publication.
    const bool stale=(withMarkets
        ? marketMaterialGradientStale[teamNumber][resourceType][swimClass]
        : materialGradientStale[teamNumber][resourceType][swimClass]);
    if ((!gradient || stale) && !map.compute.insideJob())
        requestGradientField(&gradient, int(withMarkets ? gradient_preparation::Kind::Markets : gradient_preparation::Kind::Materials), teamNumber, resourceType, movementAccessFromLegacy(swimClass));
	return gradient;
}

void MapRouting::updateMaterialGradient(int teamNumber, Uint8 resourceType, MovementAccess movementAccessValue, bool withMarkets)
{
    int swimClass=accessClass(movementAccessValue);

    if (map.game) {
        auto& field=withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
        requestGradientField(&field, int(withMarkets ? gradient_preparation::Kind::Markets : gradient_preparation::Kind::Materials), teamNumber, resourceType, movementAccessFromLegacy(swimClass)); return;
    }

	withMarkets = withMarkets && marketsV2Enabled();
	PERF_SCOPE_TIME(ResourceGradient);
	auto &slot = withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
	gradientRuntime->pipeline.invalidate(&slot);
	// A capture may share the published buffer: rewrite a fresh one instead.
	if (slot && releasePlaneLease(&slot, slot))
	{
		recordFieldCost(slot, -1);
		auto fresh = recycledPlane();
		slot = fresh ? fresh.release() : new Uint16[map.size];
	}
	Uint16 *gradient = slot;

	if(withMarkets && map.game)ensureGradientMarketTargets(teamNumber,resourceType);
	seedMaterialGradient(teamNumber, resourceType, swimClass, gradient, withMarkets);
	{
		PERF_SCOPE_TIME(PropagationResource);
		propagateGradient(gradient, swimClass, Map::GRADIENT_COST_LIMIT,
            withMarkets ? gradient_kernel::Family::Markets : gradient_kernel::Family::Materials);
        recordFieldCost(gradient,swimClass);
	}
    (withMarkets ? marketMaterialGradientStale[teamNumber][resourceType][swimClass]
                 : materialGradientStale[teamNumber][resourceType][swimClass])=false;
	publishPlane(&slot);
}

void MapRouting::seedMaterialGradient(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, bool withMarkets, const Building* consumer, unsigned modes)
{
	assert(gradient);
	if (modes || (withMarkets && marketsV2Enabled()))
		return seedMaterialGradientWithSuppliers(teamNumber, resourceType, swimClass, gradient, consumer, modes ? modes : 1);
	// Keep supplier storage and traversal out of natural-only preparation,
	// including the frequent warmed-cache path.
	if (!gradientRuntime->resourceSeeds.trySeed(map, teamNumber, resourceType, swimClass, gradient, nullptr))
		seedMaterialGradientDirect(teamNumber, resourceType, swimClass, gradient, nullptr);
}

void MapRouting::seedMaterialGradientWithSuppliers(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, const Building* consumer, unsigned modes)
{
	// Compile supplier eligibility and cost once; the map-sized loop only reads
	// compact instance-indexed values, never the building catalog.
	std::array<Uint16, Building::MAX_COUNT> supplierSeeds;
	const Team& team=*map.game->teams[teamNumber];
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
	if (!gradientRuntime->resourceSeeds.trySeed(map, teamNumber, resourceType, swimClass, gradient, supplierSeeds.data()))
		seedMaterialGradientDirect(teamNumber, resourceType, swimClass, gradient, supplierSeeds.data());
	// Overlay providers have no tile occupancy entry. Seed their small
	// footprints after either cached or direct preparation.
	if ((modes&2) || map.game->buildingsTypes.usesOverlaySuppliers())
	{
		const Uint32 teamMask=Team::teamNumberToMask(teamNumber);
		const auto *occupancy = map.occupancyCells.data();
		const auto *areas = map.areaCells.data();

		visitSuppliers([&](const Building* supplier) {
			const Uint16 seed = supplierSeeds[Building::GIDtoID(supplier->gid)];
			if (supplier->runtime->has(BuildingRuntimeTraits::OccupiesGround) || seed <= GRADIENT_UNREACHABLE) return;
			for (int y=0; y<supplier->type->height; ++y)
				for (int x=0; x<supplier->type->width; ++x)
				{
					const size_t i = map.coordToIndex(supplier->posX+x, supplier->posY+y);
					if (swimClass==AIR_MOVEMENT_CLASS || (!(areas[i].forbidden & teamMask) && occupancy[i].immobileUnit == IMMOBILE_UNIT_NONE))
						gradient[i] = std::max(gradient[i], seed);
				}
		});
	}
}

void MapRouting::seedMaterialGradientDirect(int teamNumber, Uint8 resourceType, int swimClass, Uint16 *gradient, const Uint16 *supplierSeeds)
{
    gradient_preparation::materialCells(map.liveCells, map.fogOfWar, teamNumber, resourceType, swimClass, gradient, supplierSeeds,
        [this](auto fn) { initializeGradientCells(fn); });
}

void MapRouting::dirtyMarketGradientsSlot(int teamNumber, int resourceType)
{
    if(resourceType<0 || resourceType>=MaterialCount)return;
    ++gradientRuntime->stockRevision[teamNumber][resourceType];
    if(!marketsV2Enabled())return;
    gradientSupplierTargetsChanged(teamNumber,resourceType);
    // Shared supplier edits change policy metadata. They do not submit work
    // for every allocated field; actual demand/reactive selector failures decide.

}


bool MapRouting::stockSupplierEligibleSlot(const Building* supplier, const Building* consumer, int resource, unsigned modes) const
{
    return supplier && supplier!=consumer && (!consumer || !supplier->sharesMaterialPool(*consumer))
        && supplier->buildingState==Building::ALIVE
        && ((((modes&1) ? supplier->runtime->suppliesStockMask : 0) | ((modes&2) ? supplier->runtime->suppliesDirectStockMask : 0))&(1u<<resource))
        && supplier->availableMaterial(resource)>0;
}

const Uint16* MapRouting::cachedMaterialGradientSlot(const Building* consumer, int resource, MovementAccess movementAccessValue, unsigned modes)
{
    int swim=accessClass(movementAccessValue);

    std::lock_guard<std::mutex> lock(materialGradientMutex);
    auto& runtime=*gradientRuntime;
    const int team=consumer->owner->teamNumber;
    const bool privateField=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) ||
        ((((modes&1) ? consumer->runtime->suppliesStockMask : 0) | ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0))&(1u<<resource));
    const int excluded=privateField ? consumer->gid : -1;
    const Uint64 key=MapState::materialFieldKey(excluded,team,resource,swim,modes);
    auto found=runtime.materialFields.find(key);
    if (map.compute.insideJob()) {
        if (found==runtime.materialFields.end()) return nullptr;
        const auto& entry=found->second;
        if (privateField && (entry.identity!=consumer->scriptIdentity || entry.type!=consumer->typeNum
            || entry.x!=consumer->posX || entry.y!=consumer->posY || entry.excludesShared!=consumer->usesTeamMaterials())) return nullptr;
        return entry.published;
    }
    if (found==runtime.materialFields.end())
    {
        const Uint64 fieldBytes=Uint64(map.size)*sizeof(Uint16)*(runtime.materialDestinationsEnabled?3:1);
        const Uint64 limit=std::max(fieldBytes,runtime.materialCacheBudget);
        while (!runtime.materialLru.empty() && (runtime.materialFields.size()+1)*fieldBytes>limit)
        {
            auto victim=runtime.materialLru.end();
            for (auto candidate=runtime.materialLru.begin();candidate!=runtime.materialLru.end();++candidate) {
                auto& old=runtime.materialFields.at(*candidate);
                if (!retainedMaterialField(runtime,old,map.game)) { victim=candidate; break; }
            }
            if (victim==runtime.materialLru.end()) return nullptr;
            runtime.fieldCosts.erase(runtime.materialFields.at(*victim).published);
            runtime.pipeline.forgetDestinations(&runtime.materialFields.at(*victim).published);
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
        runtime.fieldCosts.erase(entry.published);
        entry.cells.reset(); entry.published=nullptr; entry.awaitingFirstRead=false;
    }
    // Stock and topology changes request a delayed refresh; natural growth and
    // harvest also refresh at the same fixed age on every platform.
    if (!entry.published || entry.recency==0 || changedConsumer || entry.sourceRevision!=runtime.stockRevision[team][resource]
        || entry.topology!=map.topologyGeneration || Uint32(map.game->stepCounter-entry.builtStep)>=128)
    {
        bool pending=false;
        for (const auto& queued:runtime.fieldRequests) if (queued.slot==&entry.published) pending=true;
        runtime.pipeline.forEachDestination([&](auto* slot) { if (slot==&entry.published) pending=true; });
        if (swim==AIR_MOVEMENT_CLASS) {
            unsigned workReason=GradientWorkStats::Unknown;
            const auto workKey=GradientWorkStats::consumerKey(team,swim,resource,excluded<0?65535:excluded,privateField?consumer->scriptIdentity:0,modes);
            if(auto& stats=runtime.workStats) {
                stats->request(workKey,map.game->stepCounter,unsigned(entry.published?AdaptiveGradientState::Reason::Reactive:AdaptiveGradientState::Reason::Cold),false);
                workReason=stats->admitted(workKey,map.game->stepCounter,map.size);
            }
            if (!entry.cells) entry.cells=std::make_unique<Uint16[]>(map.size);
            seedMaterialGradient(team,resource,swim,entry.cells.get(),true,privateField ? consumer : nullptr,modes);
            propagateGradient(entry.cells.get(),swim,Map::GRADIENT_COST_LIMIT,gradient_kernel::Family::Markets);
            recordFieldCost(entry.cells.get(),swim);
            entry.published=entry.cells.get();
            if(auto& stats=runtime.workStats)stats->published(workKey,map.game->stepCounter,workReason,map.size);
        } else if (!map.compute.insideJob()) requestGradientField(&entry.published, int(gradient_preparation::Kind::Markets),team,resource,movementAccessFromLegacy(swim),excluded,modes);
        // Keep the revision of the admitted request. Changes observed while its
        // ticket is pending must remain dirty after that result publishes.
        if (!pending) {
            entry.sourceRevision=runtime.stockRevision[team][resource]; entry.topology=map.topologyGeneration;
            entry.builtStep=map.game->stepCounter; entry.identity=privateField ? consumer->scriptIdentity : 0;
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
void MapRouting::setMaterialRoutingCacheBudget(Uint64 bytes)
{
    auto& runtime=*gradientRuntime;
    const Uint64 fieldBytes=sizeof(Uint16)*map.size*(runtime.materialDestinationsEnabled?3:1);
    const auto target=std::clamp<Uint64>(bytes,fieldBytes,std::max<Uint64>(fieldBytes,64ull*1024*1024));
    while (materialRoutingCacheBytes()>target) {
        auto victim=runtime.materialLru.end();
        for (auto it=runtime.materialLru.begin();it!=runtime.materialLru.end();++it) {
            auto& entry=runtime.materialFields.at(*it);
            if (!retainedMaterialField(runtime,entry,map.game)) { victim=it; break; }
        }
        if (victim==runtime.materialLru.end()) break;
        runtime.fieldCosts.erase(runtime.materialFields.at(*victim).published);
        runtime.pipeline.forgetDestinations(&runtime.materialFields.at(*victim).published);
        runtime.materialFields.erase(*victim); runtime.materialLru.erase(victim);
    }
    // Configuration cannot destroy an admitted destination. A shrink takes the
    // smallest budget that still retains its logically outstanding tickets.
    runtime.materialCacheBudget=std::max(target,materialRoutingCacheBytes());
}
Uint64 MapRouting::materialRoutingCacheBytes() const
{
    return Uint64(gradientRuntime->materialFields.size())*map.size*sizeof(Uint16)*(gradientRuntime->materialDestinationsEnabled?3:1);
}

void MapRouting::setMaterialDestinationsEnabled(bool enabled)
{
    auto& runtime=*gradientRuntime;
    if (runtime.materialDestinationsEnabled==enabled) return;
    if (runtime.pipeline.pendingCount() || !runtime.preparations.empty() || !runtime.fieldRequests.empty())
        throw std::logic_error("Change destination policy only at a drained gradient boundary");
    // Old fields can still serve cost-only queries until their next refresh.
    // Clear metadata on a policy change so disabling never retains an old
    // destination interpretation or undercounts a labeled consumer cache.
    for (const auto& plane:publishedPlanes) runtime.pipeline.forgetDestinations(plane.slot);
    for (auto& [key,field]:runtime.materialFields) runtime.pipeline.forgetDestinations(&field.published);
    runtime.materialDestinationsEnabled=enabled;
    setMaterialRoutingCacheBudget(runtime.materialCacheBudget);
    if (map.game) map.game->invalidateObservation();
}
bool MapRouting::materialDestinationsEnabled() const { return gradientRuntime->materialDestinationsEnabled; }
std::shared_ptr<const gradient_kernel::OwnedDestinations> MapRouting::materialGradientDestinations(const Uint16* field) const
{ return gradientRuntime->pipeline.destinationFieldForCosts(field); }
Uint64 MapRouting::materialGradientDestinationGeneration(Uint16* const* slot) const
{ return gradientRuntime->pipeline.destinationGeneration(slot); }

unsigned MapRouting::materialSupplyModesSlot(const Building* consumer, int resource) const
{
    if (!consumer) return 1;
    const unsigned bit=1u<<resource;
    const auto& catalog=map.game->buildingsTypes;
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
    if (modes==3 && !(map.game->buildingsTypes.extraDirectSupplyMask()&bit)) modes=1;
    return modes;
}

void MapRouting::publishPlane(Uint16* const* slot)
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

std::shared_ptr<const Uint16[]> MapRouting::sharePublishedPlane(const MapState::PublishedPlane& plane) const
{
	auto& lease = gradientRuntime->planeLeases[plane.key];
	Uint16* cells = *plane.slot;
	if (!lease || lease->cells != cells)
	{
		// Every site that moves a buffer out of a material slot releases its lease.
		assert(!lease || lease.use_count() == 1);
		lease = std::make_shared<GradientRuntime::PlaneLease>();
		lease->cells = cells;
		lease->count = map.size;
		lease->recycler = gradientRuntime->planeRecycler;
	}
	return std::shared_ptr<const Uint16[]>(lease, cells);
}

std::unique_ptr<Uint16[]> MapRouting::recycledPlane()
{
	return gradientRuntime->planeRecycler->take(map.size);
}

void MapRouting::rebuildPlaneRegistry()
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

const Uint16* MapRouting::lookupMaterialGradientSlot(int team, int material, MovementAccess movementAccessValue, bool markets, const Building* consumer) const
{
    int swim=accessClass(movementAccessValue);

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
    markets=markets && marketsV2Enabled() && !map.game->teams[team]->stockSuppliers.empty();
    return markets ? marketMaterialGradients[team][material][swim] : materialGradients[team][material][swim];
}
