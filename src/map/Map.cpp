// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "ComputeExecutor.h"
#include "field/TerrainMovementCosts.h"
#include <atomic>
#include "Map.h"
#include "TerrainBuiltins.h"
#include "MapAssetBundle.h"
#include "TerrainLine.h"
#include <stdexcept>
#include "gradient/GradientRuntime.h"
#include "gradient/MapRouting.h"
#include "claims/ActiveRouteRuntime.h"
#include "gradient/GradientTargetCell.h"
#include "gradient/GradientExecutionAudit.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "render/SoftwareTerrainCache.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include <algorithm>

#include "render/GameAnimations.h"



// Definitions of shared direction tables declared in MapInternal.h.
// All Map*.cpp TUs link against these single definitions.

const int deltaOne[8][2]={
	{ 0, -1},
	{ 1,  0},
	{ 0,  1},
	{-1,  0},
	{-1, -1},
	{ 1, -1},
	{ 1,  1},
	{-1,  1}};

const int tabClose[8][2]={
	{-1, -1},
	{ 0, -1},
	{ 1, -1},
	{ 1,  0},
	{ 1,  1},
	{ 0,  1},
	{-1,  1},
	{-1,  0}};

Map::Map() : computeOwner(std::make_unique<ComputeExecutor>()), compute(*computeOwner), routing_(std::make_unique<MapRouting>(*this)), assetBundleValue(MapAssetBundle::empty())
{
	terrainCounts.assign(TERRAIN_COUNT, 0);
    rebuildTerrainCounts();
	topologyGeneration=1;
	game=NULL;

	arraysBuilt=false;
	
	for (int t = 0; t < Team::MAX_COUNT; t++)
		exploredArea[t] = NULL;
	
	sectors=NULL;
	listedAddr=NULL;
	
	for (int t = 0; t < Team::MAX_COUNT; t++)
		clearingAreaClaims[t] = NULL;
	w=0;
	h=0;
	size=0;
	wMask=0;
	hMask=0;
	wDec=0;
	hDec=0;
	wSector=0;
	hSector=0;
	sizeSector=0;
	

	areaNames.resize(9);
	
	fertilityMaximum = 0;
}

Map::~Map(void)
{
	clear();
}

std::shared_ptr<const std::vector<Uint8>> Map::frozenWaterSnapshot() const
{
	// Executor jobs may initialize together; terrain edits remain serialized by
	// simulation scheduling. This mutex protects cache creation, not terrain writes.
	std::lock_guard<std::mutex> lock(waterSnapshotMutex);
	if (!waterSnapshot)
	{
		GradientExecutionAudit::record(GradientExecutionAudit::Kind::TerrainPlane);
		auto snapshot = std::make_shared<std::vector<Uint8>>(size);
		// Called by independent executor jobs too; do not dispatch while holding
		// this lock. All readers share this one initialization pass.
		for (size_t i = 0; i < size; ++i)
			(*snapshot)[i] = terrainPropertiesAt(i).swimmable;
		waterSnapshot = std::move(snapshot);
	}
	return waterSnapshot;
}

std::shared_ptr<const std::vector<TerrainType>> Map::frozenVertexSnapshot() const
{
	std::lock_guard<std::mutex> lock(waterSnapshotMutex);
	if (!vertexSnapshot) vertexSnapshot = std::make_shared<const std::vector<TerrainType>>(vertexTerrain);
	return vertexSnapshot;
}

std::shared_ptr<const TerrainMovementSnapshot>
Map::frozenTerrainMovementSnapshot(unsigned swim) const
{
	std::lock_guard<std::mutex> lock(waterSnapshotMutex);
	auto &cached = terrainMovementSnapshots[swim];
	if (!cached)
	{
		GradientExecutionAudit::record(GradientExecutionAudit::Kind::TerrainPlane);
		auto snapshot = std::make_shared<TerrainMovementSnapshot>();
		snapshot->cells.resize(size);
		auto &movement = snapshot->movement;
		movement.steps.reserve(256);
		// Distinct entry costs in order of first use, so the profile numbering
		// depends only on the cells, never on the order rules were interned.
		std::vector<unsigned> remap(cellRuleTable->size(), 256);
		std::array<bool, 256> usedSteps{};
		for (std::size_t i = 0; i < size; ++i)
		{
			auto &profile = remap[cellRules[i]];
			if (profile == 256)
			{
				const auto costs = cellRuleData[cellRules[i]].ground[swim];
				profile = 0;
				while (profile < movement.profiles.size() && (movement.profiles[profile].cardinal != costs.cardinal ||
															  movement.profiles[profile].diagonal != costs.diagonal))
					++profile;
				if (profile == movement.profiles.size())
				{
					if (profile == 256) throw std::logic_error("Too many terrain movement profiles");
					movement.profiles.push_back(costs);
					for (auto step : {costs.cardinal, costs.diagonal})
						if (!usedSteps[step])
						{
							usedSteps[step] = true;
							movement.steps.push_back(step);
						}
				}
			}
			snapshot->cells[i] = static_cast<Uint8>(profile);
		}
		if (movement.profiles.empty()) movement.profiles.push_back(gradient_kernel::LAND_STEPS);
		movement.prepare();
		cached = std::move(snapshot);
	}
	return cached;
}

bool Map::projectilePathClear(Sint32 x0, Sint32 y0, Sint32 x1, Sint32 y1) const
{
	if (!hasProjectileBlockingTerrain()) return true;
	// Real trajectories are at most one wrapped map diameter. Bound malformed
	// imported trajectories before products or traversal can become excessive.
	if (std::abs(std::int64_t(x1)-x0) > std::int64_t(w)*TILE_PX ||
		std::abs(std::int64_t(y1)-y0) > std::int64_t(h)*TILE_PX) return false;
	return terrainSegmentClear(x0,y0,x1,y1,[&](std::int64_t x,std::int64_t y) {
		return terrainPropertiesAt(int(x & wMask),int(y & hMask)).projectileBlocks;
	});
}

void Map::adjustTerrainFeatures(std::uint16_t ruleIndex, bool add)
{
	const auto &rule = cellRuleData[ruleIndex];
	const auto &p = rule.properties;
	const unsigned edge = rule.ground[p.swimmable ? LEGACY_SWIM_CLASS_COUNT - 1 : 0].diagonal;
	for (unsigned sw = 0; sw < MOVEMENT_CLASS_COUNT; ++sw)
		if (gradient_kernel::terrainAllowsMovement(p, sw))
		{
			auto &count = terrainGroundCostCounts[sw][rule.ground[sw].cardinal];
			auto &diagonal = terrainGroundDiagonalCounts[sw][rule.ground[sw].diagonal];
			if (add)
			{
				++count;
				++diagonal;
			}
			else
			{
				assert(count && diagonal);
				--count;
				--diagonal;
			}
		}
	if (p.flyable)
	{
		auto &count = terrainAirCostCounts[rule.airCost];
		if (add)
			++count;
		else
		{
			assert(count);
			--count;
		}
	}
	refreshRouteFloors();
	const bool flags[] = {bool(p.groundHealthQ8 || p.airHealthQ8),
						  p.groundSpeedQ8 != 256 || p.groundHealthQ8 < 0,
						  !p.flyable || p.airSpeedQ8 != 256 || p.airHealthQ8 < 0,
						  p.projectileBlocks,
						  edge >= 64,
						  edge >= 128};
	for (unsigned i = 0; i < terrainFeatures.size(); ++i)
		if (flags[i])
		{
			if (add)
				++terrainFeatures[i];
			else
			{
				assert(terrainFeatures[i]);
				--terrainFeatures[i];
			}
		}
}
// Point-to-point heuristic floors: the cheapest steps among present rules. They
// follow every rule count change at once, even inside a TerrainEditBatch: a floor
// above an enterable step would make the A* heuristic inconsistent.
void Map::refreshRouteFloors()
{
	const auto first = [](const auto &counts) {
		for (unsigned cost = 1; cost < counts.size(); ++cost)
			if (counts[cost])
				return cost;
		return 0u;
	};
	for (unsigned sw = 0; sw < MOVEMENT_CLASS_COUNT; ++sw)
		terrainRouteFloors[sw] = {first(terrainGroundCostCounts[sw]), first(terrainGroundDiagonalCounts[sw])};
}
void Map::updateTerrainSummary()
{
	terrainHealthEffects = terrainFeatures[0];
	terrainMovementModifiers = terrainFeatures[1];
	airTerrainConstraints = terrainFeatures[2];
	projectileBlockingTerrain = terrainFeatures[3];
	terrainBucketCount = terrainFeatures[5] ? 256 : terrainFeatures[4] ? 128 : 64;
	terrainMinimumAir = GRADIENT_STEP;
	refreshRouteFloors();
	for (unsigned cost = 1; cost < GRADIENT_STEP; ++cost)
		if (terrainAirCostCounts[cost])
		{
			terrainMinimumAir = cost;
			break;
		}
}

void Map::importTerrainDefinitions(std::string_view json)
{
	if (game && !game->edit)
		throw std::logic_error("Terrain definitions can only change in the map editor");
    installCatalogs(terrainRegistry().importJson(json), resourceRegistryValue, assetBundleValue);
}

void Map::bindCellRules()
{
	cellRuleData = cellRuleTable->data();
	cellRuleCounts.resize(cellRuleTable->size(), 0);
	refreshLiveView();
}

void Map::rebuildTerrainCounts(std::shared_ptr<CellRuleTable> table)
{
	invalidateResourceSeeds();
	// A fresh table numbers uniform rules by terrain ID and mixed ones in
	// row-major order; a table passed in keeps the rules it already holds.
	cellRuleTable = table ? std::move(table) : std::make_shared<CellRuleTable>(terrainRegistryValue, resourceRegistryValue);
	cellRuleCounts.assign(cellRuleTable->size(), 0);
	// Size the cells before binding: the live view spans this array.
	cellRules.resize(vertexTerrain.size());
	bindCellRules();
	for (std::size_t i = 0; i < cellRules.size(); ++i)
	{
		const auto corners = cellCorners(i);
		auto rule = cellRuleTable->find(CellRuleTable::key(corners[0], corners[1], corners[2], corners[3]));
		if (!rule)
		{
			rule = cellRuleTable->intern(corners[0], corners[1], corners[2], corners[3]);
			bindCellRules();
		}
		cellRules[i] = *rule;
	}
	terrainCounts.assign(terrainRegistry().size(), 0);
	terrainFeatures.fill(0);
	terrainGroundCostCounts = {};
	terrainGroundDiagonalCounts = {};
	terrainAirCostCounts = {};
	for (const auto type : vertexTerrain) ++terrainCounts[type];
	for (const auto rule : cellRules) ++cellRuleCounts[rule];
	for (unsigned r = 0; r < cellRuleCounts.size(); ++r)
		if (cellRuleCounts[r])
			adjustTerrainFeatures(std::uint16_t(r), true);
	updateTerrainSummary();
	++terrainGenerationValue;
	terrainChanges.markAll();
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		waterSnapshot.reset();
		vertexSnapshot.reset();
		terrainMovementSnapshots = {};
	}
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.invalidate();
	}
}

std::uint16_t Map::deriveCellRule(size_t index)
{
	const auto corners = cellCorners(index);
	const auto key = CellRuleTable::key(corners[0], corners[1], corners[2], corners[3]);
	if (const auto rule = cellRuleTable->find(key)) return *rule;
	// Snapshots may still read the shared table: give the map its own copy.
	if (cellRuleTable.use_count() > 1)
		cellRuleTable = std::make_shared<CellRuleTable>(*cellRuleTable);
	const auto rule = cellRuleTable->intern(key);
	bindCellRules();
	return rule;
}

void Map::changeCellRule(size_t index, std::uint16_t rule)
{
	const auto old = cellRules[index];
	if (old == rule) return;
	const auto targetBefore=routing().gradientRuntime->adaptive.fields.empty() ? std::optional<GradientTargetCell>{}
		: std::optional{routing().captureGradientTargetCell(index)};
	if (--cellRuleCounts[old] == 0)
		adjustTerrainFeatures(old, false);
	if (cellRuleCounts[rule]++ == 0)
		adjustTerrainFeatures(rule, true);
	cellRules[index] = rule;
	markTerrain(index);
	resourceSeedChanged(index, ResourceSeedCache::Terrain);
	terrainEditChanged = true;
	// Queries inside a batch may have materialized a partial snapshot. Every
	// subsequent mutation invalidates it; generation is published at commit so
	// outside caches can never retain that partial state after the batch ends.
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		terrainMovementSnapshots = {};
		waterSnapshot.reset();
	}
	const auto &before = cellRuleData[old].properties, &after = cellRuleData[rule].properties;
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.terrainChanged(index, before, after);
	}
	if (before.walkable != after.walkable || before.swimmable != after.swimmable ||
		before.groundSpeedQ8 != after.groundSpeedQ8 || before.flyable != after.flyable ||
		before.airSpeedQ8 != after.airSpeedQ8 ||
		before.groundHealthQ8 != after.groundHealthQ8 || before.airHealthQ8 != after.airHealthQ8)
		terrainRoutesChanged = true;
	if(targetBefore) routing().gradientTargetCellChanged(index,*targetBefore);
}

void Map::writeVertex(size_t index, TerrainType type)
{
	const auto old = vertexTerrain[index];
	--terrainCounts[old];
	++terrainCounts[type];
	vertexTerrain[index] = type;
	// The vertex is drawn by the four cells around it, even when their rules hold.
	terrainEditChanged = true;
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		vertexSnapshot.reset();
	}
	const int x = int(index & wMask), y = int(index >> wDec);
	for (int dy = -1; dy <= 0; ++dy)
		for (int dx = -1; dx <= 0; ++dx)
		{
			const auto cell = size_t(coordToIndex(x + dx, y + dy));
			markTerrain(cell);
			changeCellRule(cell, deriveCellRule(cell));
		}
}

void Map::setVertexTerrain(size_t index, TerrainType type)
{
	if (index >= vertexTerrain.size()) throw std::out_of_range("Terrain vertex index");
	if (!validTerrainType(type)) throw std::invalid_argument("Unknown terrain identity");
	if (vertexTerrain[index] == type) return;
	writeVertex(index, type);
	if (!terrainEditDepth) finishTerrainEdit();
}

void Map::assignVertexTerrain(std::span<const TerrainType> vertices)
{
	if (vertices.size() != vertexTerrain.size()) throw std::invalid_argument("Terrain vertex count");
	for (const auto type : vertices)
		if (!validTerrainType(type)) throw std::invalid_argument("Unknown terrain identity");
	std::copy(vertices.begin(), vertices.end(), vertexTerrain.begin());
	rederiveAllCells();
}

void Map::rederiveAllCells()
{
	// Keep the table: its rules stay valid, and snapshots may share it.
	auto table = cellRuleTable.use_count() > 1 ? std::make_shared<CellRuleTable>(*cellRuleTable) : cellRuleTable;
	rebuildTerrainCounts(std::move(table));
	terrainEditChanged = terrainRoutesChanged = true;
	if (!terrainEditDepth) finishTerrainEdit();
}

void Map::fillTerrain(TerrainType type)
{
	assignVertexTerrain(std::vector<TerrainType>(vertexTerrain.size(), type));
}

void Map::finishTerrainEdit()
{
	if (terrainEditChanged)
	{
		++terrainGenerationValue;
		updateTerrainSummary();
	}
	if (terrainRoutesChanged && arraysBuilt)
	{
		bumpTopologyGeneration();
		for (int team = 0; team < Team::MAX_COUNT; ++team)
			for (int swim = 0; swim < MOVEMENT_CLASS_COUNT; ++swim)
			{
                // Cell edits are soft changes: publish the captured complete
                // field at its original deadline, then refresh the dirty plane.
                // Repeated painting must not starve all visible generations.
                // Escape fields use the same delayed refresh contract, including
                // cost-only terrain changes such as grass becoming trail.
				if (routing().forbiddenGradient[team][swim]) routing().updateForbiddenGradient(team, movementAccessFromLegacy(swim));
			}
	}
	terrainEditChanged = terrainRoutesChanged = false;
}

void Map::paintCell(size_t index, TerrainType type)
{
	if (index >= size) throw std::out_of_range("Terrain cell index");
	auto batch = editTerrain();
	const int x = int(index & wMask), y = int(index >> wDec);
	for (int dy = 0; dy <= 1; ++dy)
		for (int dx = 0; dx <= 1; ++dx)
			setVertexTerrain(x + dx, y + dy, type);
}

Uint16 *MapRouting::acquireBuildingGradientBuffer()
{
	{
		std::lock_guard<std::mutex> lock(gradientBufferPoolMutex);
		if (idleGradientBufferCount)
			return idleGradientBuffers[--idleGradientBufferCount];
	}
	return new Uint16[map.size];
}

void MapRouting::recycleBuildingGradientBuffer(Uint16 *buffer)
{
	if (!buffer) return;
	{
		std::lock_guard<std::mutex> lock(gradientBufferPoolMutex);
		const std::size_t byBytes = map.size && map.size <= GRADIENT_BUFFER_POOL_BYTES / sizeof(Uint16)
			? GRADIENT_BUFFER_POOL_BYTES / (map.size * sizeof(Uint16)) : 0;
		const std::size_t limit = std::min(GRADIENT_BUFFER_POOL_SLOTS, byBytes);
		if (idleGradientBufferCount < limit)
		{
			idleGradientBuffers[idleGradientBufferCount++] = buffer;
			return;
		}
	}
	delete[] buffer;
}

void MapRouting::clearGradientBufferPool()
{
	std::lock_guard<std::mutex> lock(gradientBufferPoolMutex);
	while (idleGradientBufferCount)
		delete[] idleGradientBuffers[--idleGradientBufferCount];
}

std::vector<std::pair<std::string,Uint64>> MapRouting::gradientExecutionMetrics() const
{
    std::vector<std::pair<std::string,Uint64>> result;
    if (gradientRuntime->materialDestinationsEnabled) {
        // Owner-held published allocations and immutable pending reservations
        // only: memory observation must not join work or inspect worker output.
        // Retained snapshots, workspaces and allocator slack remain part of RSS.
        const auto memory=gradientRuntime->pipeline.destinationMemory();
        result.emplace_back("destination_published_bytes",memory.publishedBytes);
        result.emplace_back("destination_pending_reserved_bytes",memory.pendingReservedBytes);
    }
    if (gradientRuntime->deviceService) {
        const auto device=gradientRuntime->deviceService->metrics();
        result.insert(result.end(),{{"coordinator_submitted",device.submitted},{"coordinator_completed",device.completed},
            {"coordinator_executed",device.executed},{"coordinator_fallbacks",device.fallbacks},
            {"coordinator_queued",device.queued}});
    }
    return result;
}

void MapRouting::configureGradientExecution()
{
    auto& rt=*gradientRuntime;
    const auto selected=gradient_kernel::configuration();
    const bool gpu=gradient_kernel::isGPU(selected.backend) && map.compute.threadCount()>1;
    // No worker-only GPU predicate is registered on the simulation executor.
    if (gpu) {
        if (globalContainer) globalContainer->gradientDeviceShutdown = &gradient_kernel::shutdownGradientDeviceCoordinator;
        if (!rt.backendSession) rt.backendSession=std::make_shared<gradient_kernel::BackendSession>();
        rt.backendSession->failed=false;
        rt.backendSession->invalidateConfiguration();
        if (!rt.deviceService) rt.deviceService=std::make_shared<gradient_kernel::GradientDeviceService>();
        rt.deviceService->configure(map.compute.threadCount(),selected.backend);
    } else {
        rt.deviceService.reset(); rt.backendSession.reset();
    }
    rt.shareBackendSession();
    rt.pipeline.setDeviceService(rt.deviceService);
    rt.pipeline.setExecutionBackend(selected);
}

void MapRouting::applyGradientBackendRequest()
{
    const auto requested=gradient_kernel::requestedBackendConfiguration();
    if(requested==gradient_kernel::configuration())return;
    // Finish privately, preserving the pending queue and original publication ticks.
    if(map.drainGameWork)map.drainGameWork(false);
    finishGradientPipeline();
    gradient_kernel::applyBackendConfiguration(requested);
    configureGradientExecution();
}

void Map::configureCompute(unsigned threads)
{
    if(drainGameWork)drainGameWork(true);
    // Retire our executor handles without publishing their future results.
    if (materialClaimRoutes) (void)materialClaimRoutes->pipeline.exportPending();
	routing().finishGradientPipeline({JoinReason::Reconfigure, ComputeTelemetry::NoTick});
	finishResourceGrowth({JoinReason::Reconfigure, ComputeTelemetry::NoTick});
	compute.configure(threads);
	routing().gradientRuntime->pipeline.resizeWorkspaces();
	routing().gradientRuntime->workspaces.resize(compute.threadCount());
	routing().gradientRuntime->shareBackendSession();
    routing().configureGradientExecution();
}

void Map::clear()
{
    materialClaimRoutes.reset();
    resourceClaimsEnabled=false;
    claimSetupTick.reset();
    activeClaimRules.reset();
    materialClaims.reset(0);
	if (game) game->areaEffects.reset();
	loadedHistoricalGrowthVersion = 0;
	loadedLegacyGrowth144 = false;
	loadedLegacyGrowth145 = false;
	routing().gradientRuntime->growth.reset();
	markAllChanges();
	routing().clearPlaneRegistry();
    bumpStaticMaterialSourceGeneration();
	static std::atomic<Uint64> nextIdentity{1};
	identityValue = nextIdentity.fetch_add(1);
	terrainSeedValue = 0;
    routing().gradientRuntime->preparations.clear();
    routing().gradientRuntime->adaptive={};
    routing().gradientRuntime->marketTargets={};
    if (auto& probe=routing().gradientRuntime->qualityProbe) {
        probe->finishStudy(probe->lastTick);probe->fields.clear();probe->window=UINT32_MAX;probe->resetMovement();
    }
    routing().gradientRuntime->fieldRequests.clear();
	routing().gradientRuntime->pipeline.reset();
    routing().gradientRuntime->materialDestinationsEnabled = false;
	// Retires into the buffer pool, so before the pool is cleared below.
	routing().resetBuildingGradientPipeline();
	routing().gradientRuntime->resetBackendSession();
    routing().configureGradientExecution();
	routing().gradientRuntime->buildingSynchronous=0;
	routing().gradientRuntime->synchronousByReason={};
	routing().gradientRuntime->overlaySupplierLocations.clear();
	routing().gradientRuntime->supplierLocationsDirty=true;
	routing().gradientRuntime->resourceSeeds.reset();
	routing().gradientRuntime->safety.reset();
    routing().gradientRuntime->directFlight = {};
	routing().clearGradientBufferPool();
	clearBuildingGradientSearchPool();
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		waterSnapshot.reset();
		vertexSnapshot.reset();
		terrainMovementSnapshots = {};
	}
	resourceStockIndices.clear();
	resourceStocks.clear();
	freeResourceStocks.clear();
	materialSourceCounts.fill(0);
	paintedAreaCounts={};
	vertexTerrain.clear();
	cellRules.clear();
	refreshLiveView();
	cellRuleCounts.assign(cellRuleCounts.size(), 0);
	terrainCounts.assign(terrainRegistry().size(), 0);
	terrainFeatures.fill(0);
	terrainGroundCostCounts = {};
	terrainGroundDiagonalCounts = {};
	terrainAirCostCounts = {};
	updateTerrainSummary();
	terrainEditChanged = terrainRoutesChanged = false;
	++terrainGenerationValue;
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.invalidate();
	}
	growthCoverage.clear();
	for (auto &counts : growthCoverageCounts) counts.clear();
	for (auto &buildings : growthCoverageBuildings) buildings.clear();
	growthCoverageValid = false;
	routing().gradientRuntime->materialFields.clear();
	routing().gradientRuntime->fieldCosts.clear();
	routing().gradientRuntime->materialLru.clear();
	routing().gradientRuntime->stockRevision={};
	routing().gradientRuntime->materialCacheClock=0;
	routing().gradientRuntime->materialCacheBudget=64ull*1024*1024;
	topologyGeneration=1;
	// A failed load can own only a subset of these arrays.
	for (int t=0; t<Team::MAX_COUNT; ++t)
	{
		for (int r=0; r<MaterialCount; ++r)
			for (int swim=0; swim<MOVEMENT_CLASS_COUNT; ++swim)
			{
				delete[] routing().materialGradients[t][r][swim];
				routing().materialGradientStale[t][r][swim]=false;
				routing().materialGradients[t][r][swim] = NULL;
				delete[] routing().marketMaterialGradients[t][r][swim];
				routing().marketMaterialGradientStale[t][r][swim]=false;
				routing().marketMaterialGradients[t][r][swim] = NULL;
			}
		for (int swim=0; swim<MOVEMENT_CLASS_COUNT; ++swim)
		{
			delete[] routing().forbiddenGradient[t][swim];
			routing().forbiddenGradientStale[t][swim]=false;
			routing().forbiddenGradient[t][swim] = NULL;
			delete[] routing().guardAreasGradient[t][swim];
			routing().guardGradientStale[t][swim]=false;
			routing().guardAreasGradient[t][swim] = NULL;
			delete[] routing().clearAreasGradient[t][swim];
			routing().clearGradientStale[t][swim]=false;
			routing().clearAreasGradient[t][swim] = NULL;
		}
		delete[] exploredArea[t];
		exploredArea[t] = NULL;
		delete[] clearingAreaClaims[t];
		clearingAreaClaims[t] = NULL;
	}
	delete[] sectors;
	sectors = NULL;
	delete[] listedAddr;
	listedAddr = NULL;

	arraysBuilt = false;

	w=h=0;
	size=0;
	wMask=hMask=0;
	wDec=hDec=0;
	wSector=hSector=0;
	sizeSector=0;
	displayedTeam = NO_DISPLAYED_TEAM;

}


void Map::setSize(int wDec, int hDec, TerrainType terrainType)
{
    // Dimensions are exponents, never arbitrary cell counts. Validate before
    // shifting or clearing the existing map, including in release builds.
    if (wDec < 0 || hDec < 0 || wDec >= 16 || hDec >= 16)
        throw std::invalid_argument("Map dimensions require power-of-two size exponents in [0, 15]");
    if (!resourceRegistryValue->size())
    {
        resourceRegistryValue = ResourceRegistry::builtins();
        refreshLiveView();
        rebuildTerrainCounts();
    }
	if (!validTerrainType(terrainType)) throw std::invalid_argument("Unknown terrain identity");

	clear();

	this->wDec=wDec;
	this->hDec=hDec;
	w=1<<wDec;
	h=1<<hDec;
	wMask=w-1;
	hMask=h-1;
	size=w*h;

	fogOfWarA.assign(size, 0);
	fogOfWarB.assign(size, 0);
	fogOfWar = &fogOfWarA[0];
	
	displayedForbiddenView.resize(size, false);
	displayedGuardAreaView.resize(size, false);
	displayedClearAreaView.resize(size, false);
	displayedFarmAreaView.resize(size, false);
	
	resourceCells.assign(size, {});
	for (auto &cell : resourceCells) cell.mayGrow = 1;
	occupancyCells.assign(size, {});
	areaCells.assign(size, {});
	scriptAreaCells.assign(size, 0);
	vertexTerrain.assign(size, terrainType);
	resetChangeTracking();
	rebuildTerrainCounts();

	mapDiscovered.assign(size, 0);
	
	listedAddr = new Uint8*[size];

	//numberOfTeam=0, then resourcesGradient[][][] is empty. This is done by clear();

	wSector=w>>Sector::SECTOR_SHIFT;
	hSector=h>>Sector::SECTOR_SHIFT;
	sizeSector=wSector*hSector;

	if(sectors)
		delete[] sectors;
	sectors=new Sector[sizeSector];





	arraysBuilt=true;
	finishTerrainEdit();
}


void Map::setGame(Game *game)
{
	assert(game);
	this->game=game;
	finishTerrainEdit();
	assert(arraysBuilt);
	assert(sectors);
	for (int i=0; i<sizeSector; i++)
		sectors[i].setGame(game);
	game->animations->resize(sizeSector);
}

void Map::setTerrainSeed(Uint32 seed)
{
    if (terrainSeedValue == seed) return;
    terrainSeedValue = seed;
    if (game) game->invalidateObservation();
}

EntityRandom& Map::privateRandom(RandomDomain domain)
{
    // Standalone authoring maps have a fixed seed until a request sets one.
    return worldRandom.get(game ? game->gameHeader.getRandomSeed() : 0, domain);
}

// Shared by seed preparation and mutation metadata as well as resource queries.
bool Map::farmAreasEnabled() const
{
	return game && game->gameHeader.hasExperiment(ExperimentId::FarmAreas);
}
