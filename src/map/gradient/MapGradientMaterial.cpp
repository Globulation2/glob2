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
#include <atomic>
#include <array>
#include "SeedCells.h"
#include <type_traits>

Uint16 *Map::getMaterialGradientSlot(int teamNumber, int resourceType, int swimClass, bool withMarkets, const Building* consumer)
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
	// Compute jobs (hiring searches, AI-enrolled fields) may request the same
	// lazy field concurrently. Cover both allocation and pipeline invalidation
	// before publishing the pointer.
	std::lock_guard<std::mutex> lock(materialGradientMutex);
	if (!withMarkets && !hasMaterialSourceSlot(resourceType))
	{
		auto& empty = gradientRuntime->absentMaterialField;
		if (empty.size() != size) empty.assign(size, GRADIENT_UNREACHABLE);
		return empty.data();
	}
	Uint16 *&gradient = withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
    const bool stale=withMarkets ? marketMaterialGradientStale[teamNumber][resourceType][swimClass]
                                 : materialGradientStale[teamNumber][resourceType][swimClass];
    if (gradient == NULL || stale) {
        if (!gradient) gradient = new Uint16[size];
        updateMaterialGradient(teamNumber, resourceType, swimClass, withMarkets);
    }
    if(!withMarkets && ResourceExperiment::reader==ResourceExperiment::Reader::Bulk)
        gradientRuntime->resolveResource(&gradient,0,ResourceExperiment::Reader::Bulk);
	return gradient;
}

Uint16 Map::getGradient(int team,Uint8 material,int swim,int x,int y,bool markets,const Building* consumer)
{
    ResourceExperiment::ReadScope scope(ResourceExperiment::Reader::Scalar);
    auto* data=getMaterialGradientSlot(team,material,swim,markets,consumer);
    std::lock_guard<std::mutex> lock(materialGradientMutex);
    if(data==materialGradients[team][material][swim])gradientRuntime->resolveResource(&materialGradients[team][material][swim],coordToIndex(x,y),ResourceExperiment::Reader::Scalar);
    return data[coordToIndex(x,y)];
}
void Map::finishResourceGradients()
{
    std::lock_guard<std::mutex> lock(materialGradientMutex);
    for(auto& [slot,field]:gradientRuntime->resourceSearches)
        if(field.data==*slot)gradientRuntime->resolveResource(slot,0,ResourceExperiment::Reader::Export);
}

void Map::updateMaterialGradient(int teamNumber, Uint8 resourceType, int swimClass, bool withMarkets)
{
	withMarkets = withMarkets && marketsV2Enabled();
	PERF_SCOPE_TIME(ResourceGradient);
	auto &slot = withMarkets ? marketMaterialGradients[teamNumber][resourceType][swimClass] : materialGradients[teamNumber][resourceType][swimClass];
	gradientRuntime->pipeline.invalidate(&slot);
	Uint16 *gradient = slot;
    if(!withMarkets) {
        auto& d=gradientRuntime->demand[teamNumber][resourceType][swimClass];
        gradientRuntime->initializeAdaptiveSources(*this);
        d.servingSources=gradientRuntime->adaptiveSources[resourceType];
        d.captured=d.serving=d.lastSubmitted=game?game->stepCounter:0;d.servingChanges=snapshotGenerations();
    }
    gradientRuntime->retireResource(&slot,game?game->stepCounter:0);
	if (auto& probe=gradientRuntime->qualityProbe)
    {probe->captured[gradient]=game ? game->stepCounter : 0;probe->captureSources(gradient,resourceType);}
	seedMaterialGradient(teamNumber, resourceType, swimClass, gradient, withMarkets);
	{
		PERF_SCOPE_TIME(PropagationResource);
        if(!withMarkets && gradientRuntime->lazyActive) {
            ResourceExperiment::Lazy field;field.data=gradient;field.capture=field.published=game?game->stepCounter:0;
            field.key=(teamNumber*MaterialCount+resourceType)*SWIM_CLASS_COUNT+swimClass;field.width=getW();field.height=getH();
            const int units=game?game->teams[teamNumber]->stats.measurements().groundUnitsByMovementClass[swimClass]:0;
            field.features=gradientRuntime->depthFeatures(&slot,field.key,units);
            const int depth=gradientRuntime->depthModel.active()?std::clamp(gradientRuntime->depthModel.predict(field.features),0,65535):std::max(0,gradientRuntime->depthMode);
            Uint16* data=gradient;
            if(gradientRuntime->depthCensus){field.shadow.assign(data,data+size);data=field.shadow.data();}
            field.search=acquireBuildingGradientSearch();field.search->begin(*this,data,swimClass);if(depth>0)field.search->resolveToCost(depth);
            field.resolveOwner=[this,pointer=&slot](size_t cell){gradientRuntime->resolveResource(pointer,cell,ResourceExperiment::Reader::Scalar);};
            gradientRuntime->resourceSearches[&slot]=std::move(field);
            ResourceSnapshotExperiment::registerSlot(&slot,&gradientRuntime->resourceSearches[&slot]);
            if(gradientRuntime->depthCensus)propagateGradient(gradient,swimClass,GRADIENT_COST_LIMIT,gradient_kernel::Family::Materials);
        } else propagateGradient(gradient, swimClass, GRADIENT_COST_LIMIT,
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
					if (!(areas[i].forbidden & teamMask) && occupancy[i].immobileUnit == IMMOBILE_UNIT_NONE)
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
	for (int s=0; s<SWIM_CLASS_COUNT; ++s)
		{
			marketGradientDirty[teamNumber][resourceType][s]=true;
			gradientRuntime->pipeline.invalidate(&marketMaterialGradients[teamNumber][resourceType][s]);
		}
}


bool Map::stockSupplierEligibleSlot(const Building* supplier, const Building* consumer, int resource, unsigned modes) const
{
    return supplier && supplier!=consumer && (!consumer || !supplier->sharesMaterialPool(*consumer))
        && supplier->buildingState==Building::ALIVE
        && ((((modes&1) ? supplier->runtime->suppliesStockMask : 0) | ((modes&2) ? supplier->runtime->suppliesDirectStockMask : 0))&(1u<<resource))
        && supplier->availableMaterial(resource)>0;
}

Uint16* Map::cachedMaterialGradientSlot(const Building* consumer, int resource, int swim, unsigned modes)
{
    std::lock_guard<std::mutex> lock(materialGradientMutex);
    auto& runtime=*gradientRuntime;
    const int team=consumer->owner->teamNumber;
    const bool privateField=consumer->runtime->has(BuildingRuntimeTraits::SharedStock) ||
        ((((modes&1) ? consumer->runtime->suppliesStockMask : 0) | ((modes&2) ? consumer->runtime->suppliesDirectStockMask : 0))&(1u<<resource));
    const int excluded=privateField ? consumer->gid : -1;
    const Uint64 key=MapState::materialFieldKey(excluded,team,resource,swim,modes);
    auto found=runtime.materialFields.find(key);
    if (found==runtime.materialFields.end())
    {
        const Uint64 fieldBytes=Uint64(size)*sizeof(Uint16);
        const Uint64 limit=std::max(fieldBytes,runtime.materialCacheBudget);
        while (!runtime.materialLru.empty() && (runtime.materialFields.size()+1)*fieldBytes>limit)
        {
            runtime.materialFields.erase(runtime.materialLru.front());
            runtime.materialLru.pop_front();
        }
        found=runtime.materialFields.try_emplace(key).first;
        auto& entry=found->second;
        entry.cells=std::make_unique<Uint16[]>(size);
        entry.consumer=excluded; entry.team=team; entry.resource=resource; entry.swim=swim; entry.modes=modes;
        runtime.materialLru.push_back(key); entry.lru=std::prev(runtime.materialLru.end());
    }
    auto& entry=found->second;
    const bool changedConsumer=privateField && (entry.identity!=consumer->scriptIdentity || entry.type!=consumer->typeNum
        || entry.x!=consumer->posX || entry.y!=consumer->posY);
    // Synchronous bounded fields use the same fixed age across platforms. Stock
    // changes invalidate immediately; natural growth/harvest refreshes by age.
    if (entry.recency==0 || changedConsumer || entry.sourceRevision!=runtime.stockRevision[team][resource]
        || entry.topology!=topologyGeneration || Uint32(game->stepCounter-entry.builtStep)>=128)
    {
        seedMaterialGradient(team,resource,swim,entry.cells.get(),true,privateField ? consumer : nullptr,modes);
        {
            PERF_SCOPE_TIME(PropagationResource);
            propagateGradient(entry.cells.get(),swim,GRADIENT_COST_LIMIT,gradient_kernel::Family::Markets);
        }
        entry.sourceRevision=runtime.stockRevision[team][resource]; entry.topology=topologyGeneration;
        entry.builtStep=game->stepCounter; entry.identity=privateField ? consumer->scriptIdentity : 0;
        entry.type=privateField ? consumer->typeNum : -1; entry.x=privateField ? consumer->posX : 0; entry.y=privateField ? consumer->posY : 0;
    }
    entry.recency=++runtime.materialCacheClock;
    runtime.materialLru.splice(runtime.materialLru.end(),runtime.materialLru,entry.lru);
    return entry.cells.get();
}
void Map::setMaterialRoutingCacheBudget(Uint64 bytes)
{
    gradientRuntime->materialCacheBudget=std::clamp<Uint64>(bytes,sizeof(Uint16)*size,std::max<Uint64>(sizeof(Uint16)*size,64ull*1024*1024));
    while (!gradientRuntime->materialLru.empty() && materialRoutingCacheBytes()>gradientRuntime->materialCacheBudget)
    {
        gradientRuntime->materialFields.erase(gradientRuntime->materialLru.front());
        gradientRuntime->materialLru.pop_front();
    }
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
	constexpr auto perArray = std::size_t(Team::MAX_COUNT) * MaterialSlotCount * SWIM_CLASS_COUNT;
	const auto decode = [&](Uint16* const* first, bool market) {
		const auto offset = std::size_t(slot - first);
		return MapState::planeKey(int(offset / (MaterialSlotCount * SWIM_CLASS_COUNT)),
			int((offset / SWIM_CLASS_COUNT) % MaterialSlotCount), int(offset % SWIM_CLASS_COUNT), market);
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
			for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
			{
				if (materialGradients[team][resource][swim]) publishPlane(&materialGradients[team][resource][swim]);
				if (marketMaterialGradients[team][resource][swim]) publishPlane(&marketMaterialGradients[team][resource][swim]);
			}
}

// Frozen resource-field cache. Registry keys are buffer identities only; readers
// obtain a captured search and never borrow a live map or mutable owner field.
namespace ResourceSnapshotExperiment {
namespace {
struct Snapshot;
struct Registry {
 std::mutex mutex;
 std::map<Uint16**,ResourceExperiment::Lazy*> slots;
 std::map<const Uint16*,Uint16**> owners;
 std::map<const std::vector<Uint16>*,std::weak_ptr<Snapshot>> snapshots;
 std::array<std::atomic<std::uint64_t>,4> counts{};
 std::atomic<unsigned> slotCount{0},snapshotCount{0};
};
Registry& registry(){static auto* value=new Registry;return *value;}
struct Snapshot {
 std::shared_ptr<std::vector<Uint16>> values;
 std::vector<Uint16> shadow;
 std::unique_ptr<BuildingGradientSearch> search;
 std::mutex mutex;int width=0,height=0,key=0,required=0;unsigned capture=0,reads=0,fullReads=0;bool registered=false;std::uint64_t baseExpanded=0;std::vector<int> features;
 ~Snapshot(){
  if(!registered)return;
  {auto& r=registry();std::lock_guard lock(r.mutex);r.snapshots.erase(values.get());--r.snapshotCount;}
  if(const char* path=std::getenv("GLOB2_RESOURCE_DEPTH_CSV")) {
   static std::mutex logMutex;std::lock_guard lock(logMutex);
   static std::ofstream log(std::string(path)+".snapshots");
   if(log.tellp()==0)log<<"key,capture,required,queries,full_reads,entries,base_entries,serving_required,previous_required,serving_queries,previous_queries,material,swim,class_units\n";
   log<<key<<','<<capture<<','<<required<<','<<reads<<','<<fullReads<<','<<search->expandedEntries()<<','<<baseExpanded;
   for(int f:features)log<<','<<f;log<<'\n';
  }
  recycleBuildingGradientSearch(std::move(search));
 }
 void at(size_t cell) {
  const auto before=search->expandedEntries();search->resolveForProposal(cell);
  const auto* data=shadow.empty()?values->data():shadow.data();
  const auto demand=[&](size_t i){const auto value=data[i];return value==GRADIENT_UNREACHABLE?search->settledCost():(value>GRADIENT_UNREACHABLE&&value!=GRADIENT_AT_GOAL?65535-int(value)+1:0);};
  required=std::max(required,demand(cell));
  if(data[cell]<=GRADIENT_UNREACHABLE) {
   const int x=cell%width,y=cell/width;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
    {const size_t i=size_t(((y+dy)&(height-1))*width+((x+dx)&(width-1)));search->resolveForProposal(i);required=std::max(required,demand(i));}
  }
  ++reads;
  if(!shadow.empty() && shadow[cell]!=(*values)[cell])throw std::runtime_error("Frozen resource snapshot mismatch");
  registry().counts[2].fetch_add(search->expandedEntries()-before,std::memory_order_relaxed);
 }
 void all() {
  const auto before=search->expandedEntries();search->finish();
  ++reads;++fullReads;required=std::max(required,search->settledCost());
  if(!shadow.empty() && shadow!=*values)throw std::runtime_error("Frozen resource snapshot full mismatch");
  registry().counts[2].fetch_add(search->expandedEntries()-before,std::memory_order_relaxed);
 }
};
std::shared_ptr<Snapshot> find(const std::shared_ptr<const std::vector<Uint16>>& values) {
 auto& r=registry();if(!r.snapshotCount.load(std::memory_order_relaxed))return {};
 std::lock_guard lock(r.mutex);const auto it=r.snapshots.find(values.get());return it==r.snapshots.end()?nullptr:it->second.lock();
}
}
void registerSlot(Uint16** slot,ResourceExperiment::Lazy* field){auto& r=registry();std::lock_guard lock(r.mutex);if(r.slots.contains(slot))throw std::logic_error("Duplicate resource slot registration");r.slots[slot]=field;r.owners[field->data]=slot;++r.slotCount;}
void unregisterSlot(Uint16** slot){auto& r=registry();std::lock_guard lock(r.mutex);const auto it=r.slots.find(slot);if(it!=r.slots.end()){const auto owner=r.owners.find(it->second->data);if(owner!=r.owners.end()&&owner->second==slot)r.owners.erase(owner);r.slots.erase(it);--r.slotCount;}}
bool resolveOwner(const Uint16* values,size_t cell){
 auto& r=registry();if(!r.slotCount.load(std::memory_order_relaxed))return false;
 std::function<void(size_t)> resolve;
 {std::lock_guard lock(r.mutex);const auto owner=r.owners.find(values);if(owner==r.owners.end()||*owner->second!=values)return false;const auto field=r.slots.find(owner->second);if(field==r.slots.end()||field->second->data!=values)return false;resolve=field->second->resolveOwner;}
 if(!resolve)return false;resolve(cell);return true;
}
std::shared_ptr<std::vector<Uint16>> clonePlane(Uint16* const* slot,std::shared_ptr<std::vector<Uint16>> values) {
 auto& r=registry();if(!r.slotCount.load(std::memory_order_relaxed))return values;
 std::lock_guard lock(r.mutex);const auto it=r.slots.find(const_cast<Uint16**>(slot));
 if(it==r.slots.end()||it->second->data!=*slot||it->second->search->complete())return values;
 const auto& field=*it->second;
 auto snapshot=std::make_shared<Snapshot>();snapshot->values=std::move(values);snapshot->width=field.width;snapshot->height=field.height;snapshot->key=field.key;snapshot->capture=field.capture;snapshot->features=field.features;snapshot->baseExpanded=field.search->expandedEntries();
 Uint16* data=snapshot->values->data();
 if(!field.shadow.empty()){snapshot->shadow=field.shadow;data=snapshot->shadow.data();}
 snapshot->search=field.search->clone(data);
 r.snapshots[snapshot->values.get()]=snapshot;snapshot->registered=true;++r.snapshotCount;++r.counts[0];
 return std::shared_ptr<std::vector<Uint16>>(snapshot,snapshot->values.get());
}
void finish(const std::shared_ptr<const std::vector<Uint16>>& values){if(auto snapshot=find(values)){std::lock_guard lock(snapshot->mutex);++registry().counts[1];++registry().counts[3];snapshot->all();}}
void resolve(const std::shared_ptr<const std::vector<Uint16>>& values,size_t cell,std::unique_lock<std::mutex>& guard){if(auto snapshot=find(values)){guard=std::unique_lock(snapshot->mutex);++registry().counts[1];snapshot->at(cell);}}
std::array<std::uint64_t,4> metrics(){auto& r=registry();return {r.counts[0].load(),r.counts[1].load(),r.counts[2].load(),r.counts[3].load()};}
}
