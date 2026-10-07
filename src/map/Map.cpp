// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <atomic>
#include "Map.h"
#include "TerrainCompatibility.h"
#include "TerrainLine.h"
#include <stdexcept>
#include "gradient/GradientRuntime.h"
#include "Game.h"
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

Map::Map() : gradientRuntime(std::make_unique<GradientRuntime>())
{
	topologyGeneration=1;
	game=NULL;

	arraysBuilt=false;
	
	aStarPoints = NULL;
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_NB_RESOURCES; r++)
			for (int s=0; s<SWIM_CLASS_COUNT; s++)
			{
				resourcesGradient[t][r][s] = NULL;
				gradientUpdated[t][r][s] = false;
				marketResourcesGradient[t][r][s] = NULL;
				marketGradientDirty[t][r][s] = false;
				marketGradientUpdated[t][r][s] = false;
			}
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int s=0; s<SWIM_CLASS_COUNT; s++)
		{
			forbiddenGradient[t][s] = NULL;
			guardAreasGradient[t][s] = NULL;
			clearAreasGradient[t][s] = NULL;
			guardGradientUpdated[t][s] = false;
			clearGradientUpdated[t][s] = false;
		}
	for (int t = 0; t < Team::MAX_COUNT; t++)
		exploredArea[t] = NULL;
	
	undermap=NULL;
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
		auto snapshot = std::make_shared<std::vector<Uint8>>(size);
		// Called by independent executor jobs too; do not dispatch while holding
		// this lock. All readers share this one initialization pass.
		for (size_t i = 0; i < size; ++i)
			(*snapshot)[i] = terrainPropertiesAt(i).swimmable;
		waterSnapshot = std::move(snapshot);
	}
	return waterSnapshot;
}

std::shared_ptr<const std::vector<TerrainType>> Map::frozenTerrainSnapshot() const
{
	std::lock_guard<std::mutex> lock(waterSnapshotMutex);
	if (!terrainSnapshot) terrainSnapshot = std::make_shared<const std::vector<TerrainType>>(terrainIds);
	return terrainSnapshot;
}

std::shared_ptr<const TerrainMovementSnapshot>
Map::frozenTerrainMovementSnapshot(unsigned swim) const
{
	std::lock_guard<std::mutex> lock(waterSnapshotMutex);
	auto &cached = terrainMovementSnapshots[swim];
	if (!cached)
	{
		auto snapshot = std::make_shared<TerrainMovementSnapshot>();
		snapshot->cells.resize(size);
		const auto &source = terrainRegistry().movement(swim);
		auto &movement = snapshot->movement;
		movement.profiles.reserve(source.profiles.size());
		movement.steps.reserve(256);
		std::array<unsigned, 256> remap;
		remap.fill(256);
		std::array<bool, 256> usedSteps{};
		for (std::size_t i = 0; i < size; ++i)
		{
			const auto original = source.profileIds[terrainIds[i]];
			auto &profile = remap[original];
			if (profile == 256)
			{
				profile = movement.profiles.size();
				const auto costs = source.profiles[original];
				movement.profiles.push_back(costs);
				for (auto step : {costs.cardinal, costs.diagonal})
					if (!usedSteps[step])
					{
						usedSteps[step] = true;
						movement.steps.push_back(step);
					}
			}
			snapshot->cells[i] = static_cast<Uint8>(profile);
		}
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

ExperimentSet Map::requiredTerrainExperiments() const
{
	ExperimentSet required;
	for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
		if (terrainCounts[t])
			if (const auto experiment = terrainExperiment(static_cast<TerrainType>(t))) required.set(*experiment);
	return required;
}

void Map::adjustTerrainFeatures(TerrainType type, bool add)
{
	const auto &p = terrainProperties(type);
	const unsigned edge =
		gradient_kernel::entrySteps(
			gradient_kernel::scaledTerrainStep(
				p.swimmable ? GRADIENT_SLOWEST_SWIM_STEP : GRADIENT_STEP, p.groundSpeedQ8))
			.diagonal;
	for (unsigned sw = 0; sw < 7; ++sw)
		if (p.walkable || (sw && p.swimmable))
		{
			auto &count =
				terrainGroundCostCounts[sw][terrainRegistry().movement(sw).entries[type].cardinal];
			if (add)
				++count;
			else
			{
				assert(count);
				--count;
			}
		}
	if (p.flyable)
	{
		auto &count = terrainAirCostCounts[terrainRegistry().airCost(type)];
		if (add)
			++count;
		else
		{
			assert(count);
			--count;
		}
	}
	const bool flags[] = {bool(p.groundHealthQ8 || p.airHealthQ8),
						  p.groundSpeedQ8 != 256,
						  !p.flyable || p.airSpeedQ8 != 256,
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
void Map::updateTerrainSummary()
{
	terrainHealthEffects = terrainFeatures[0];
	terrainMovementModifiers = terrainFeatures[1];
	airTerrainConstraints = terrainFeatures[2];
	projectileBlockingTerrain = terrainFeatures[3];
	terrainBucketCount = terrainFeatures[5] ? 256 : terrainFeatures[4] ? 128 : 64;
	terrainMinimumGround = gradient_kernel::MINIMUM_TERRAIN_ENTRY_COSTS;
	terrainMinimumAir = GRADIENT_STEP;
	for (unsigned sw = 0; sw < 7; ++sw)
		for (unsigned cost = 1; cost < terrainMinimumGround[sw]; ++cost)
			if (terrainGroundCostCounts[sw][cost])
			{
				terrainMinimumGround[sw] = cost;
				break;
			}
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
	auto next = terrainRegistry().importJson(json);
	// Compilation/validation and allocation happen before publishing a replacement.
	std::vector<std::size_t> counts(next->size());
	std::vector<Uint16> propertyIndices(terrainIds.size());
	for (std::size_t i = 0; i < terrainIds.size(); ++i)
	{
		++counts[terrainIds[i]];
		propertyIndices[i] = next->propertyIndex(terrainIds[i]);
	}
	finishGradientPipeline();
	terrainRegistryValue = std::move(next);
	invalidateResourceSeeds();
	terrainPropertyIndices = std::move(propertyIndices);
	terrainPropertyTable = terrainRegistry().propertyProfiles().data();
	terrainCounts = std::move(counts);
	terrainFeatures.fill(0);
	terrainGroundCostCounts = {};
	terrainAirCostCounts = {};
	for (unsigned t = 0; t < terrainCounts.size(); ++t)
		if (terrainCounts[t])
			adjustTerrainFeatures(TerrainType(t), true);
	for (std::size_t i = 0; i < terrainIds.size(); ++i)
	{
		const auto &p = terrainRegistry().compatibility(terrainIds[i]);
		if (!p.legacyCorners)
			legacyTerrain[i] =
				p.firstFrame + terrainVisualHash(int(i & wMask), int(i >> wDec)) % p.variants;
	}
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.invalidate();
	}
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		terrainSnapshot.reset();
		terrainMovementSnapshots = {};
		waterSnapshot.reset();
	}
	if (arraysBuilt && marketsV2Enabled())
		for (int team = 0; team < Team::MAX_COUNT; ++team)
			for (int resource = 0; resource < MAX_RESOURCES; ++resource)
				for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
				{
					gradientRuntime->pipeline.invalidate(
						&marketResourcesGradient[team][resource][swim]);
					marketGradientUpdated[team][resource][swim] = false;
					marketGradientDirty[team][resource][swim] = true;
				}
	terrainEditChanged = terrainRoutesChanged = true;
	finishTerrainEdit();
}

void Map::rebuildTerrainCounts()
{
	invalidateResourceSeeds();
	terrainPropertyTable = terrainRegistry().propertyProfiles().data();
	terrainPropertyIndices.resize(terrainIds.size());
	for (std::size_t i = 0; i < terrainIds.size(); ++i)
		terrainPropertyIndices[i] = terrainRegistry().propertyIndex(terrainIds[i]);
	terrainCounts.assign(terrainRegistry().size(), 0);
	terrainFeatures.fill(0);
	terrainGroundCostCounts = {};
	terrainAirCostCounts = {};
	for (const auto type : terrainIds) ++terrainCounts[type];
	for (unsigned t = 0; t < terrainCounts.size(); ++t)
		if (terrainCounts[t])
			adjustTerrainFeatures(TerrainType(t), true);
	updateTerrainSummary();
	++terrainGenerationValue;
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		waterSnapshot.reset();
		terrainSnapshot.reset();
		terrainMovementSnapshots = {};
	}
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.invalidate();
	}
}

void Map::importLegacyTerrain()
{
	// This adapter may also be used by imports on an existing map. Validate
	// first, then pass every semantic change through the normal invalidation.
	for (const auto sprite : legacyTerrain)
		if (sprite >= 272) throw std::invalid_argument("Invalid legacy terrain sprite");
	if (terrainIds.size()!=cellCount())
	{
		terrainIds.assign(cellCount(),GRASS);
		rebuildTerrainCounts();
	}
	auto batch = editTerrain();
	for (size_t i = 0; i < cellCount(); ++i)
		changeTerrainIdentity(i, legacyTerrainType(legacyTerrain[i]));
}

void Map::changeTerrainIdentity(size_t index, TerrainType type)
{
	if (!validTerrainType(type)) throw std::invalid_argument("Unknown terrain identity");
	const TerrainType old = terrainIds[index];
	if (old == type) return;
	if (--terrainCounts[old] == 0)
		adjustTerrainFeatures(old, false);
	if (terrainCounts[type]++ == 0)
		adjustTerrainFeatures(type, true);
	terrainIds[index] = type;
	terrainPropertyIndices[index] = terrainRegistry().propertyIndex(type);
	resourceSeedChanged(index, ResourceSeedCache::Terrain);
	terrainEditChanged = true;
	// Queries inside a batch may have materialized a partial snapshot. Every
	// subsequent mutation invalidates it; generation is published at commit so
	// outside caches can never retain that partial state after the batch ends.
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		terrainSnapshot.reset();
		terrainMovementSnapshots = {};
		waterSnapshot.reset();
	}
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.terrainChanged(index, terrainProperties(old), terrainProperties(type));
	}
	const auto &before = terrainProperties(old), &after = terrainProperties(type);
	if (before.walkable != after.walkable || before.swimmable != after.swimmable ||
		before.groundSpeedQ8 != after.groundSpeedQ8 || before.flyable != after.flyable ||
		before.airSpeedQ8 != after.airSpeedQ8)
		terrainRoutesChanged = true;
	if (!terrainEditDepth) finishTerrainEdit();
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
			for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
			{
				// A queued field owns an immutable snapshot of the old terrain.
				// Prevent its delayed publication from undoing this invalidation.
				gradientRuntime->pipeline.invalidate(&guardAreasGradient[team][swim]);
				gradientRuntime->pipeline.invalidate(&clearAreasGradient[team][swim]);
				guardGradientUpdated[team][swim] = clearGradientUpdated[team][swim] = false;
				for (int resource = 0; resource < MAX_RESOURCES; ++resource)
				{
					gradientRuntime->pipeline.invalidate(&resourcesGradient[team][resource][swim]);
					gradientUpdated[team][resource][swim] = false;
				}
				// Escape fields have no dirty flag. Their scheduled seed comparison
				// cannot detect a cost-only change such as grass becoming trail.
				if (forbiddenGradient[team][swim]) updateForbiddenGradient(team, swim);
			}
	}
	terrainEditChanged = terrainRoutesChanged = false;
	if (game)
	{
		game->mapHeader.requiredTerrainExperiments = requiredTerrainExperiments();
		for (const auto& definition : experimentDefinitions())
			if (game->mapHeader.requiredTerrainExperiments.has(definition.id)) game->gameHeader.getExperiments().set(definition.id);
	}
}

void Map::setTerrain(int x, int y, Uint16 sprite)
{
	if (sprite >= 272) throw std::invalid_argument("Legacy terrain setter requires a legacy frame");
	const auto index = coordToIndex(x,y);
	changeTerrainIdentity(index, legacyTerrainType(sprite));
	legacyTerrain[index] = sprite;
	++snapshotTerrain;
}

void Map::setCellTerrain(size_t index, TerrainType type)
{
	++snapshotTerrain;
	if (index >= size) throw std::out_of_range("Terrain cell index");
	changeTerrainIdentity(index, type);
	const auto &p = terrainRegistry().compatibility(type);
	legacyTerrain[index] =
		p.firstFrame + terrainVisualHash(int(index & wMask), int(index >> wDec)) % p.variants;
}

Uint16 *Map::acquireBuildingGradientBuffer()
{
	{
		std::lock_guard<std::mutex> lock(gradientBufferPoolMutex);
		if (idleGradientBufferCount)
			return idleGradientBuffers[--idleGradientBufferCount];
	}
	return new Uint16[size];
}

void Map::recycleBuildingGradientBuffer(Uint16 *buffer)
{
	if (!buffer) return;
	{
		std::lock_guard<std::mutex> lock(gradientBufferPoolMutex);
		const std::size_t byBytes = size && size <= GRADIENT_BUFFER_POOL_BYTES / sizeof(Uint16)
			? GRADIENT_BUFFER_POOL_BYTES / (size * sizeof(Uint16)) : 0;
		const std::size_t limit = std::min(GRADIENT_BUFFER_POOL_SLOTS, byBytes);
		if (idleGradientBufferCount < limit)
		{
			idleGradientBuffers[idleGradientBufferCount++] = buffer;
			return;
		}
	}
	delete[] buffer;
}

void Map::clearGradientBufferPool()
{
	std::lock_guard<std::mutex> lock(gradientBufferPoolMutex);
	while (idleGradientBufferCount)
		delete[] idleGradientBuffers[--idleGradientBufferCount];
}

void Map::configureCompute(unsigned threads, unsigned experiments)
{
	preparePendingGradient();
	compute.configure(threads);
	gradientRuntime->workspaces.resize(compute.threadCount());
	computeExperiments = experiments;
}

void Map::clear()
{
	++snapshotTerrain; ++snapshotResources; ++snapshotOccupancy; ++snapshotAreas; ++snapshotVisibility;
	resourceFieldGenerations.clear();
	static std::atomic<Uint64> nextIdentity{1};
	identityValue = nextIdentity.fetch_add(1);
	terrainSeedValue = 0;
	gradientRuntime->preparation={};
	gradientRuntime->pipeline.reset();
	gradientRuntime->overlaySupplierLocations.clear();
	gradientRuntime->supplierLocationsDirty=true;
	gradientRuntime->resourceSeeds.reset();
	clearGradientBufferPool();
	clearBuildingGradientSearchPool();
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		waterSnapshot.reset();
		terrainSnapshot.reset();
		terrainMovementSnapshots = {};
	}
	terrainIds.clear();
	terrainPropertyIndices.clear();
	terrainCounts.assign(terrainRegistry().size(), 0);
	terrainFeatures.fill(0);
	terrainGroundCostCounts = {};
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
	gradientRuntime->resourceFields.clear();
	gradientRuntime->resourceLru.clear();
	gradientRuntime->stockRevision={};
	gradientRuntime->resourceCacheClock=0;
	gradientRuntime->resourceCacheBudget=64ull*1024*1024;
	topologyGeneration=1;
	// A failed load can own only a subset of these arrays.
	for (int t=0; t<Team::MAX_COUNT; ++t)
	{
		for (int r=0; r<MAX_RESOURCES; ++r)
			for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
			{
				delete[] resourcesGradient[t][r][swim];
				resourcesGradient[t][r][swim] = NULL;
				gradientUpdated[t][r][swim] = false;
				delete[] marketResourcesGradient[t][r][swim];
				marketResourcesGradient[t][r][swim] = NULL;
				marketGradientDirty[t][r][swim] = false;
				marketGradientUpdated[t][r][swim] = false;
			}
		for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
		{
			delete[] forbiddenGradient[t][swim];
			forbiddenGradient[t][swim] = NULL;
			delete[] guardAreasGradient[t][swim];
			guardAreasGradient[t][swim] = NULL;
			delete[] clearAreasGradient[t][swim];
			clearAreasGradient[t][swim] = NULL;
			guardGradientUpdated[t][swim] = false;
			clearGradientUpdated[t][swim] = false;
		}
		delete[] exploredArea[t];
		exploredArea[t] = NULL;
		delete[] clearingAreaClaims[t];
		clearingAreaClaims[t] = NULL;
	}
	delete[] undermap;
	undermap = NULL;
	delete[] sectors;
	sectors = NULL;
	delete[] listedAddr;
	listedAddr = NULL;
	delete[] aStarPoints;
	aStarPoints = NULL;

	arraysBuilt = false;

	w=h=0;
	size=0;
	wMask=hMask=0;
	wDec=hDec=0;
	wSector=hSector=0;
	sizeSector=0;
	displayedTeam = NO_DISPLAYED_TEAM;

	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_RESOURCES; r++)
			for (int s=0; s<SWIM_CLASS_COUNT; s++)
				gradientUpdated[t][r][s]=false;
}


void Map::setSize(int wDec, int hDec, TerrainType terrainType)
{
	if (!validTerrainType(terrainType)) throw std::invalid_argument("Unknown terrain identity");

	clear();

	assert(wDec<16);
	assert(hDec<16);
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
	legacyTerrain.assign(size, 0);
	scriptAreaCells.assign(size, 0);
	terrainIds.assign(size, GRASS);
	terrainPropertyIndices.assign(size, terrainRegistry().propertyIndex(GRASS));
	terrainPropertyTable = terrainRegistry().propertyProfiles().data();
	terrainCounts[GRASS] = size;
	adjustTerrainFeatures(GRASS, true);

	mapDiscovered.assign(size, 0);
	
	undermap=new Uint8[size];
	memset(undermap, terrainType <= GRASS ? terrainType : GRASS, size);
	
	listedAddr = new Uint8*[size];

	//numberOfTeam=0, then resourcesGradient[][][] is empty. This is done by clear();

	auto terrainBatch = editTerrain();
	regenerateMap(0, 0, w, h);
	if (terrainType > GRASS)
		for (size_t i = 0; i < size; ++i) setCellTerrain(i, terrainType);

	wSector=w>>Sector::SECTOR_SHIFT;
	hSector=h>>Sector::SECTOR_SHIFT;
	sizeSector=wSector*hSector;

	if(sectors)
		delete[] sectors;
	sectors=new Sector[sizeSector];

	aStarPoints=new AStarAlgorithmPoint[w*h];




	arraysBuilt=true;
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
