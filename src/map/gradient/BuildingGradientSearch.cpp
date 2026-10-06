// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "BuildingGradientSearch.h"
#include "BuildingGradientDiagnostics.h"
#include <mutex>
#include "Map.h"
#include "MapInternal.h"
#include "field/RuntimeTerrainGradient.h"
#include "field/TerrainGradient.h"

using gradient_kernel::BUCKETS;
using gradient_kernel::COST_LIMIT;
using gradient_kernel::EntrySteps;
using gradient_kernel::LAND_STEPS;
using gradient_kernel::WATER_STEP;
using gradient_kernel::entrySteps;
using gradient_kernel::expandBucket;
using gradient_kernel::weightedClass;

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim, int gid)
{
 BuildingGradientInputs inputs;
 inputs.modified=map.hasTerrainMovementModifiers(); inputs.registry=map.frozenTerrainRegistry();
 inputs.buckets=map.terrainQueueBuckets();
 const bool dynamic=inputs.modified && inputs.registry->size()>TERRAIN_COUNT;
 inputs.profiles=dynamic ? map.frozenTerrainMovementSnapshot(swim) : nullptr;
 inputs.water=!inputs.modified && weightedClass(swim) && inputs.registry->size()>TERRAIN_COUNT ? map.frozenWaterSnapshot() : nullptr;
 inputs.terrain=(weightedClass(swim)||inputs.modified) && !dynamic && !inputs.water ? map.frozenTerrainSnapshot() : nullptr;
 beginFrozen(map.getW(),map.getH(),seeded,swim,inputs,COST_LIMIT);
 attach(map,seeded,gid,map.topologyGeneration);
}
void BuildingGradientSearch::attach(const Map &map, std::uint16_t *field, int gid, std::uint32_t generation)
{ sourceMap=&map; buildingId=gid; snapshotGeneration=generation; gradient=field; }
void BuildingGradientSearch::beginFrozen(int width,int height,std::uint16_t *seeded,int swim,
 const BuildingGradientInputs &inputs,int limit,int settled)
{
 assert(width>0 && height>0 && !(width&(width-1)) && !(height&(height-1)));
 assert(swim>=0 && swim<SWIM_CLASS_COUNT && limit>=0 && settled>=0);
 sourceMap=nullptr; buildingId=-1; gradient=seeded; cells=std::size_t(width)*height;
 widthMask=width-1; heightMask=height-1; swimClass=swim; currentCost=settled;
 popped=0; pending=nextSeed=0; costLimit=std::min(limit,COST_LIMIT);
 terrain=inputs.terrain; registry=inputs.registry; profiles=inputs.profiles; water=inputs.water;
 modifiedCosts=inputs.modified; terrainBuckets=inputs.buckets;
 for(auto &bucket:buckets) bucket.clear();
 deferredSeeds.clear();
 const bool dynamic=bool(profiles);
 if(dynamic) { gradient_kernel::runtime_terrain::validateQueue(profiles->movement,terrainBuckets);
  if(!custom) custom=std::make_unique<TerrainGradientWorkspace>();
  custom->prepare(terrainBuckets); for(auto &bucket:custom->buckets) bucket.clear(); }
 auto *queues=dynamic ? custom->buckets.data() : buckets.data();
 const unsigned count=dynamic ? terrainBuckets : BUCKETS;
 const unsigned maximum=dynamic ? terrainBuckets-1 : gradient_kernel::MAX_STEP;
 for(std::size_t i=0;i<cells;++i) if(gradient[i]>GRADIENT_UNREACHABLE) {
  const int cost=GRADIENT_AT_GOAL-gradient[i]; if(cost<settled) continue;
  if(unsigned(cost-settled)<=maximum) { queues[unsigned(cost)%count].push(static_cast<Uint32>(i)); ++pending; }
  else deferredSeeds.emplace_back(cost,static_cast<Uint32>(i));
 }
 std::sort(deferredSeeds.begin(),deferredSeeds.end());
}
bool BuildingGradientSearch::resolved(std::size_t target) const
{
	assert(target < cells);
	const auto value = gradient[target];
	return complete() || value == GRADIENT_FORBIDDEN || value == GRADIENT_AT_GOAL
		|| (value > GRADIENT_UNREACHABLE && GRADIENT_AT_GOAL - value < currentCost);
}

void BuildingGradientSearch::resolve(std::size_t target, const char *caller)
{
	assert(target <= cells);
	if (complete() || (target < cells && resolved(target))) return;
	if (!sourceMap) { advance(target); return; } // Pure private worker execution.
	PERF_SCOPE_TIME(BuildingGradientResume);
	BuildingGradientDiagnostics::Scope evidence(sourceMap ? sourceMap->buildingGradientDiagnostics() : nullptr,
												buildingId, swimClass,
												target == cells ? "finish" : "resume", caller,
												sourceMap ? sourceMap->topologyGeneration : snapshotGeneration, snapshotGeneration);
	const auto before = popped;
	advance(target);
	evidence.result(popped - before, complete());
}

void BuildingGradientSearch::advance(std::size_t target)
{
 const field::Grid geometry{widthMask+1,heightMask+1};
 const bool dynamic=bool(profiles); const unsigned count=dynamic ? terrainBuckets : BUCKETS;
 auto *queues=dynamic ? custom->buckets.data() : buckets.data();
 while(!complete() && (target==cells || !resolved(target))) {
  if(!pending && nextSeed<deferredSeeds.size()) currentCost=deferredSeeds[nextSeed].first;
  if(currentCost>costLimit) { pending=0; nextSeed=deferredSeeds.size();
   for(unsigned i=0;i<count;++i) queues[i].clear(); break; }
  while(nextSeed<deferredSeeds.size() && deferredSeeds[nextSeed].first==currentCost) {
   queues[unsigned(currentCost)%count].push(deferredSeeds[nextSeed++].second); ++pending; }
  popped+=queues[unsigned(currentCost)%count].size;
  if(dynamic) {
   auto layer=[&]<unsigned N>() { gradient_kernel::runtime_terrain::expandProfileBucket<N>(
    gradient,queues,pending,currentCost,costLimit,geometry,profiles->movement,
    [&](std::size_t i){return profiles->cells[i];}); };
   if(count==64) layer.template operator()<64>(); else if(count==128) layer.template operator()<128>(); else layer.template operator()<256>();
  } else if(modifiedCosts) gradient_kernel::expandTerrainBucket(gradient,queues,pending,currentCost,costLimit,geometry,
   gradient_kernel::PREPARED_TERRAIN_COSTS[swimClass],[&](std::size_t i){return (*terrain)[i];});
  else if(!weightedClass(swimClass)) expandBucket<false>(gradient,queues,pending,currentCost,costLimit,geometry,LAND_STEPS,[](std::size_t){return false;});
  else if(water) expandBucket<true>(gradient,queues,pending,currentCost,costLimit,geometry,entrySteps(WATER_STEP[swimClass]),[&](std::size_t i){return (*water)[i]!=0;});
  else expandBucket<true>(gradient,queues,pending,currentCost,costLimit,geometry,entrySteps(WATER_STEP[swimClass]),[&](std::size_t i){return gradient_kernel::terrainUsesSwimming((*terrain)[i]);});
  ++currentCost;
 }
 if(complete()) { terrain.reset(); profiles.reset(); water.reset(); }
}
std::vector<std::uint16_t> BuildingGradientSearch::completePrivateSnapshot() const
{
 if(!cells) return {};
 std::vector<std::uint16_t> result(gradient,gradient+cells); if(complete()) return result;
 BuildingGradientInputs inputs; inputs.terrain=terrain; inputs.registry=registry; inputs.profiles=profiles;
 inputs.water=water; inputs.modified=modifiedCosts; inputs.buckets=terrainBuckets;
 BuildingGradientSearch copy; copy.beginFrozen(widthMask+1,heightMask+1,result.data(),swimClass,inputs,costLimit,currentCost);
 copy.finish("private_snapshot"); return result;
}
std::size_t BuildingGradientSearch::retainedBytes() const
{
	std::size_t bytes = sizeof(*this) + deferredSeeds.capacity()*sizeof(deferredSeeds[0]);
	for (const auto &bucket : buckets)
		bytes += bucket.cells.capacity() * sizeof(bucket.cells[0]);
	if (custom)
	{
		bytes += sizeof(*custom) + custom->buckets.capacity() * sizeof(GradientBucket);
		for (const auto &b : custom->buckets)
			bytes += b.cells.capacity() * sizeof(std::uint32_t);
	}
	return bytes;
}

void BuildingGradientSearch::clearForReuse()
{
	gradient = nullptr;
	cells = pending = nextSeed = 0;
 deferredSeeds.clear(); sourceMap=nullptr;
	terrain.reset();
	registry.reset();
	profiles.reset();
	water.reset();
	if (custom)
		for (auto &b : custom->buckets)
			b.clear();
	for (auto &bucket : buckets)
		bucket.clear();
}

namespace
{
struct SearchPool
{
	// Covers the observed burst of detached searches while bounding retained
	// bucket storage across games and worker threads.
	static constexpr std::size_t SLOTS = 64;
	static constexpr std::size_t BYTES = 8 * 1024 * 1024;
	std::mutex mutex;
	std::array<std::unique_ptr<BuildingGradientSearch>, SLOTS> searches;
	std::size_t count = 0, bytes = 0;
};

SearchPool &searchPool()
{
	static SearchPool pool;
	return pool;
}
} // namespace

std::unique_ptr<BuildingGradientSearch> acquireBuildingGradientSearch()
{
	auto &pool = searchPool();
	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		if (pool.count)
		{
			auto search = std::move(pool.searches[--pool.count]);
			pool.bytes -= search->retainedBytes();
			return search;
		}
	}
	return std::make_unique<BuildingGradientSearch>();
}

void recycleBuildingGradientSearch(std::unique_ptr<BuildingGradientSearch> search)
{
	if (!search)
		return;
	search->clearForReuse();
	const std::size_t bytes = search->retainedBytes();
	auto &pool = searchPool();
	std::lock_guard<std::mutex> lock(pool.mutex);
	if (pool.count < SearchPool::SLOTS && bytes <= SearchPool::BYTES - pool.bytes)
	{
		pool.bytes += bytes;
		pool.searches[pool.count++] = std::move(search);
	}
}

void clearBuildingGradientSearchPool()
{
	auto &pool = searchPool();
	std::lock_guard<std::mutex> lock(pool.mutex);
	while (pool.count)
		pool.searches[--pool.count].reset();
	pool.bytes = 0;
}
