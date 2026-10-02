// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "Game.h"
#ifndef YOG_SERVER_ONLY
#include "render/SoftwareTerrainCache.h"
#endif
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include <algorithm>

#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY



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
	gradientRuntime->pipeline.reset();
	clearGradientBufferPool();
	clearBuildingGradientSearchPool();
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
#ifndef YOG_SERVER_ONLY
    if (game) game->softwareTerrainCache.reset();
#endif

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
	
	tiles.assign(size, Tile());

	mapDiscovered.assign(size, 0);
	
	undermap=new Uint8[size];
	memset(undermap, terrainType, size);
	
	listedAddr = new Uint8*[size];

	//numberOfTeam=0, then resourcesGradient[][][] is empty. This is done by clear();

	regenerateMap(0, 0, w, h);

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
	assert(arraysBuilt);
	assert(sectors);
	for (int i=0; i<sizeSector; i++)
		sectors[i].setGame(game);
#ifndef YOG_SERVER_ONLY
	game->animations->resize(sizeSector);
#endif  // !YOG_SERVER_ONLY
}
