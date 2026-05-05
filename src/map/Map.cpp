// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


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

const int tabFar[16][2]={
	{-2, -2},
	{-1, -2},
	{ 0, -2},
	{ 1, -2},
	{ 2, -2},
	{ 2, -1},
	{ 2,  0},
	{ 2,  1},
	{ 2,  2},
	{ 1,  2},
	{ 0,  2},
	{-1,  2},
	{-2,  2},
	{-2,  1},
	{-2,  0},
	{-2, -1}};

Map::Map()
{
	game=NULL;

	arraysBuilt=false;
	
	aStarPoints = NULL;
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_NB_RESSOURCES; r++)
			for (int s=0; s<2; s++)
			{
				ressourcesGradient[t][r][s] = NULL;
				gradientUpdated[t][r][s] = false;
			}
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int s=0; s<2; s++)
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
	
	//Gradients stats:
	for (int t=0; t<16; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
		{
			ressourceAvailableCount[t][r]=0;
			ressourceAvailableCountSuccess[t][r]=0;
			ressourceAvailableCountFailure[t][r]=0;
		}
	
	pathToRessourceCountTot=0;
	pathToRessourceCountSuccess=0;
	pathToRessourceCountFailure=0;
	
	localRessourcesUpdateCount=0;
	pathfindLocalRessourceCount=0;
	pathfindLocalRessourceCountWait=0;
	pathfindLocalRessourceCountSuccessBase=0;
	pathfindLocalRessourceCountSuccessLocked=0;
	pathfindLocalRessourceCountSuccessUpdate=0;
	pathfindLocalRessourceCountSuccessUpdateLocked=0;
	pathfindLocalRessourceCountFailureUnusable=0;
	pathfindLocalRessourceCountFailureNone=0;
	pathfindLocalRessourceCountFailureBad=0;
	
	pathToBuildingCountTot=0;
	pathToBuildingCountClose=0;
	pathToBuildingCountCloseSuccessStand=0;
	pathToBuildingCountCloseSuccessBase=0;
	pathToBuildingCountCloseSuccessUpdated=0;
	pathToBuildingCountCloseFailureLocked=0;
	pathToBuildingCountCloseFailureEnd=0;
	
	pathToBuildingCountIsFar=0;
	pathToBuildingCountFar=0;
	pathToBuildingCountFarIsNew=0;
	pathToBuildingCountFarOldSuccess=0;
	pathToBuildingCountFarOldFailureLocked=0;
	pathToBuildingCountFarOldFailureBad=0;
	pathToBuildingCountFarOldFailureRepeat=0;
	pathToBuildingCountFarOldFailureUnusable=0;
	pathToBuildingCountFarUpdateSuccess=0;
	pathToBuildingCountFarUpdateFailureLocked=0;
	pathToBuildingCountFarUpdateFailureVirtual=0;
	pathToBuildingCountFarUpdateFailureBad=0;
	
	localBuildingGradientUpdate=0;
	localBuildingGradientUpdateLocked=0;
	globalBuildingGradientUpdate=0;
	globalBuildingGradientUpdateLocked=0;
	
	buildingAvailableCountTot=0;
	buildingAvailableCountClose=0;
	buildingAvailableCountCloseSuccessFast=0;
	buildingAvailableCountCloseSuccessAround=0;
	buildingAvailableCountCloseSuccessUpdate=0;
	buildingAvailableCountCloseSuccessUpdateAround=0;
	buildingAvailableCountCloseFailureLocked=0;
	buildingAvailableCountCloseFailureEnd=0;
	
	buildingAvailableCountIsFar=0;
	buildingAvailableCountFar=0;
	buildingAvailableCountFarNew=0;
	buildingAvailableCountFarNewSuccessFast=0;
	buildingAvailableCountFarNewSuccessClosely=0;
	buildingAvailableCountFarNewFailureLocked=0;
	buildingAvailableCountFarNewFailureVirtual=0;
	buildingAvailableCountFarNewFailureEnd=0;
	buildingAvailableCountFarOld=0;
	buildingAvailableCountFarOldSuccessFast=0;
	buildingAvailableCountFarOldSuccessAround=0;
	buildingAvailableCountFarOldFailureLocked=0;
	buildingAvailableCountFarOldFailureEnd=0;
	
	pathfindForbiddenCount=0;
	pathfindForbiddenCountSuccess=0;
	pathfindForbiddenCountFailure=0;
	
	#ifdef check_disorderable_gradient_error_probability
	// stats to check the probability of an error:
	for (int i = 0; i < GT_SIZE; i++)
	{
		listCountSizeStats[i] = NULL;
		listCountSizeStatsOver[i] = 0;
	}
	#endif
	
	logFile = globalContainer->logFileManager->getFile("Map.log");
	std::fill(incRessourceLog, incRessourceLog + 16, 0);

	areaNames.resize(9);
	
	fertilityMaximum = 0;
}

Map::~Map(void)
{
	FILE *resLogFile = globalContainer->logFileManager->getFile("IncRessourceLog.log");
	for (int i=0; i<=11; i++)
		fprintf(resLogFile, "incRessourceLog[%2d] =%8d\n", i, incRessourceLog[i]);
	fprintf(resLogFile, "\n");
	fflush(resLogFile);
	clear();
}

void Map::clear()
{
	logAtClear();
	if (arraysBuilt)
	{
		for (int t=0; t<Team::MAX_COUNT; t++)
			if (ressourcesGradient[t][0][0])
				for (int r=0; r<MAX_RESSOURCES; r++)
					for (int s=0; s<2; s++)
					{
						assert(ressourcesGradient[t][r][s]);
						delete[] ressourcesGradient[t][r][s];
						ressourcesGradient[t][r][s] = NULL;
					}
		
		for (int t=0; t<Team::MAX_COUNT; t++)
			if (forbiddenGradient[t][0])
				for (int s=0; s<2; s++)
				{
					assert(forbiddenGradient[t][s]);
					delete[] forbiddenGradient[t][s];
					forbiddenGradient[t][s] = NULL;
					assert(guardAreasGradient[t][s]);
					delete[] guardAreasGradient[t][s];
					guardAreasGradient[t][s] = NULL;
					assert(clearAreasGradient[t][s]);
					delete[] clearAreasGradient[t][s];
					clearAreasGradient[t][s] = NULL;
					
					guardGradientUpdated[t][s] = false;
					clearGradientUpdated[t][s] = false;
				}
		
		for (int t=0; t<Team::MAX_COUNT; t++)
			if (exploredArea[t])
			{
				assert(exploredArea[t]);
				delete[] exploredArea[t];
				exploredArea[t] = NULL;
			}
		
		assert(undermap);
		delete[] undermap;
		undermap=NULL;
		
		assert(sectors);
		delete[] sectors;
		sectors=NULL;

		assert(listedAddr);
		delete[] listedAddr;
		listedAddr=NULL;
		
		assert(aStarPoints);
		delete[] aStarPoints;
		aStarPoints=NULL;

		for (int t=0; t<Team::MAX_COUNT; t++)
		{
			if (clearingAreaClaims[t])
			{
				assert(clearingAreaClaims[t]);
				delete[] clearingAreaClaims[t];
				clearingAreaClaims[t]=NULL;
			}
		}
		
		assert(immobileUnits);
		delete[] immobileUnits;
		immobileUnits=NULL;

		arraysBuilt=false;
	}
	else
	{
		for (int t=0; t<Team::MAX_COUNT; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
					assert(ressourcesGradient[t][r][s]==NULL);
		for (int t=0; t<Team::MAX_COUNT; t++)
			for (int s=0; s<2; s++)
			{
				assert(forbiddenGradient[t][s] == NULL);
				assert(guardAreasGradient[t][s] == NULL);
				assert(clearAreasGradient[t][s] == NULL);
			}
		for (int t=0; t<Team::MAX_COUNT; t++)
			assert(exploredArea[t] == NULL);
		for (int t=0; t<Team::MAX_COUNT; t++)
			assert(clearingAreaClaims[t] == NULL);
		
		assert(undermap==NULL);
		assert(sectors==NULL);
		assert(listedAddr==NULL);

		assert(w==0);
		assert(h==0);
		assert(size==0);
		assert(wMask==0);
		assert(hMask==0);
		assert(wDec==0);
		assert(hDec==0);
		assert(wSector==0);
		assert(hSector==0);
		assert(sizeSector==0);
	}
	
	w=h=0;
	size=0;
	wMask=hMask=0;
	wDec=hDec=0;
	wSector=hSector=0;
	sizeSector=0;
	
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				gradientUpdated[t][r][s]=false;
}


void Map::setSize(int wDec, int hDec, TerrainType terrainType)
{
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
	
	localForbiddenMap.resize(size, false);
	localGuardAreaMap.resize(size, false);
	localClearAreaMap.resize(size, false);
	
	cases.assign(size, Case());

	mapDiscovered.assign(size, 0);
	
	undermap=new Uint8[size];
	memset(undermap, terrainType, size);
	
	listedAddr = new Uint8*[size];

	//numberOfTeam=0, then ressourcesGradient[][][] is empty. This is done by clear();

	regenerateMap(0, 0, w, h);

	wSector=w>>4;
	hSector=h>>4;
	sizeSector=wSector*hSector;

	if(sectors)
		delete[] sectors;
	sectors=new Sector[sizeSector];

	aStarPoints=new AStarAlgorithmPoint[w*h];


	immobileUnits = new Uint8[w*h];
	for (int i=0; i<w*h; i++) 
	{
		immobileUnits[i]=0;
	}

	arraysBuilt=true;
	
	#ifdef check_disorderable_gradient_error_probability
	// stats to check the probability of an error:
	for (int i = 0; i < GT_SIZE; i++)
	{
		if (listCountSizeStats[i])
			delete[] listCountSizeStats[i];
		listCountSizeStats[i] = new int[size];
		listCountSizeStatsOver[i] = 0;
	}
	#endif
}


void Map::setGame(Game *game)
{
	assert(game);
	fprintf(logFile, "Map::setGame(%p)\n", game);
	this->game=game;
	assert(arraysBuilt);
	assert(sectors);
	for (int i=0; i<sizeSector; i++)
		sectors[i].setGame(game);
	
	#ifdef check_disorderable_gradient_error_probability
	// stats to check the probability of an error:
	for (int i = 0; i < GT_SIZE; i++)
	{
		if (listCountSizeStats[i])
			delete[] listCountSizeStats[i];
		listCountSizeStats[i] = new int[size];
		listCountSizeStatsOver[i] = 0;
	}
	#endif
}


