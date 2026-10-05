// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <atomic>
#include "Map.h"
#include "TerrainPresentation.h"
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
	
	immobileUnits=NULL;

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
		for (size_t i = 0; i < size; ++i) (*snapshot)[i] = isWater(static_cast<unsigned>(i));
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

void Map::updateTerrainSummary()
{
	terrainHealthEffects = terrainMovementModifiers = airTerrainConstraints = projectileBlockingTerrain = false;
	for (unsigned t=0;t<TERRAIN_COUNT;++t)
		if (terrainCounts[t])
		{
			const auto& p = TERRAIN_PROPERTIES[t];
			terrainHealthEffects |= p.groundHealthQ8 || p.airHealthQ8;
			terrainMovementModifiers |= p.groundSpeedQ8 != 256;
			airTerrainConstraints |= !p.flyable || p.airSpeedQ8 != 256;
			projectileBlockingTerrain |= p.projectileBlocks;
		}
}

void Map::rebuildTerrainCounts()
{
	terrainCounts.fill(0);
	for (const auto type : terrainIds) ++terrainCounts[type];
	updateTerrainSummary();
	++terrainGenerationValue;
	std::lock_guard<std::mutex> lock(waterSnapshotMutex);
	waterSnapshot.reset();
	terrainSnapshot.reset();
}

void Map::importLegacyTerrain()
{
	// This adapter may also be used by imports on an existing map. Validate
	// first, then pass every semantic change through the normal invalidation.
	for (const auto& tile : tiles)
		if (tile.terrain >= 272) throw std::invalid_argument("Invalid legacy terrain sprite");
	if (terrainIds.size()!=tiles.size())
	{
		terrainIds.assign(tiles.size(),GRASS);
		rebuildTerrainCounts();
	}
	auto batch = editTerrain();
	for (size_t i = 0; i < tiles.size(); ++i)
		changeTerrainIdentity(i, legacyTerrainType(tiles[i].terrain));
}

void Map::changeTerrainIdentity(size_t index, TerrainType type)
{
	if (!validTerrainType(type)) throw std::invalid_argument("Unknown terrain identity");
	const TerrainType old = terrainIds[index];
	if (old == type) return;
	--terrainCounts[old];
	++terrainCounts[type];
	terrainIds[index] = type;
	terrainEditChanged = true;
	// Queries inside a batch may have materialized a partial snapshot. Every
	// subsequent mutation invalidates it; generation is published at commit so
	// outside caches can never retain that partial state after the batch ends.
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		terrainSnapshot.reset();
		waterSnapshot.reset();
	}
	{
		std::lock_guard<std::mutex> lock(growthCacheMutex);
		growthCache.invalidate();
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
				// cannot detect a cost-only change such as grass becoming road.
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
	tiles[index].terrain = sprite;
}

void Map::setCellTerrain(size_t index, TerrainType type)
{
	if (index >= size) throw std::out_of_range("Terrain cell index");
	changeTerrainIdentity(index, type);
	tiles[index].terrain = terrainVisualFrame(type, int(index & wMask), int(index >> wDec));
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
	compute.configure(threads);
	gradientRuntime->workspaces.resize(compute.threadCount());
	computeExperiments = experiments;
}

void Map::clear()
{
	static std::atomic<Uint64> nextIdentity{1};
	identityValue = nextIdentity.fetch_add(1);
	gradientRuntime->pipeline.reset();
	clearGradientBufferPool();
	clearBuildingGradientSearchPool();
	{
		std::lock_guard<std::mutex> lock(waterSnapshotMutex);
		waterSnapshot.reset();
		terrainSnapshot.reset();
	}
	terrainIds.clear();
	terrainCounts.fill(0);
	terrainHealthEffects = terrainMovementModifiers = airTerrainConstraints = projectileBlockingTerrain = false;
	terrainEditChanged = terrainRoutesChanged = false;
	++terrainGenerationValue;
	growthCoverage.clear();
	for (auto &counts : growthCoverageCounts) counts.clear();
	for (auto &buildings : growthCoverageBuildings) buildings.clear();
	growthCoverageValid = false;
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
	delete[] immobileUnits;
	immobileUnits = NULL;
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
	
	tiles.assign(size, Tile());
	terrainIds.assign(size, GRASS);
	terrainCounts[GRASS] = size;

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


	immobileUnits = new Uint8[w*h];
	memset(immobileUnits, IMMOBILE_UNIT_NONE, w*h);

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
