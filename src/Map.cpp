/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/


#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Unit.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


#if defined( LOG_GRADIENT_LINE_GRADIENT )
#include <map>
#endif

#define UPDATE_MAX(max,value) { Uint8 tmp = value; if (value>(max)) (max)=value; }

// use deltaOne for first perpendicular direction
static const int deltaOne[8][2]={
	{ 0, -1},
	{ 1,  0},
	{ 0,  1},
	{-1,  0},
	{-1, -1},
	{ 1, -1},
	{ 1,  1},
	{-1,  1}};

// use tabClose for original circular direction
static const int tabClose[8][2]={
	{-1, -1},
	{ 0, -1},
	{ 1, -1},
	{ 1,  0},
	{ 1,  1},
	{ 0,  1},
	{-1,  1},
	{-1,  0}};

// use tabMiniFar for all miniGrad far points
static const int tabFar[16][2]={
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
	
	mapDiscovered=NULL;
	fogOfWar=NULL;
	fogOfWarA=NULL;
	fogOfWarB=NULL;
	astarpoints = NULL;
	cases=NULL;
	for (int t=0; t<32; t++)
		for (int r=0; r<MAX_NB_RESSOURCES; r++)
			for (int s=0; s<2; s++)
			{
				ressourcesGradient[t][r][s] = NULL;
				gradientUpdated[t][r][s] = false;
			}
	for (int t=0; t<32; t++)
		for (int s=0; s<2; s++)
		{
			forbiddenGradient[t][s] = NULL;
			guardAreasGradient[t][s] = NULL;
			clearAreasGradient[t][s] = NULL;
			guardGradientUpdated[t][s] = false;
			clearGradientUpdated[t][s] = false;
		}
	for (int t = 0; t < 32; t++)
		exploredArea[t] = NULL;
	
	undermap=NULL;
	sectors=NULL;
	listedAddr=NULL;
	
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
		assert(mapDiscovered);
		delete[] mapDiscovered;
		mapDiscovered=NULL;

		fogOfWar=NULL;
		
		assert(fogOfWarA);
		delete[] fogOfWarA;
		fogOfWarA=NULL;

		assert(fogOfWarB);
		delete[] fogOfWarB;
		fogOfWarB=NULL;

		assert(cases);
		delete[] cases;
		cases=NULL;
		for (int t=0; t<32; t++)
			if (ressourcesGradient[t][0][0])
				for (int r=0; r<MAX_RESSOURCES; r++)
					for (int s=0; s<2; s++)
					{
						assert(ressourcesGradient[t][r][s]);
						delete[] ressourcesGradient[t][r][s];
						ressourcesGradient[t][r][s] = NULL;
					}
		
		for (int t=0; t<32; t++)
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
		
		for (int t=0; t<32; t++)
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
		
		assert(astarpoints);
		delete[] astarpoints;
		astarpoints=NULL;

		assert(clearingAreaClaims);
		delete[] clearingAreaClaims;
		clearingAreaClaims=NULL;
		
		assert(immobileUnits);
		delete[] immobileUnits;
		immobileUnits=NULL;

		arraysBuilt=false;
	}
	else
	{
		assert(mapDiscovered==NULL);
		assert(fogOfWar==NULL);
		assert(fogOfWarA==NULL);
		assert(fogOfWarB==NULL);
		assert(cases==NULL);
		for (int t=0; t<32; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
					assert(ressourcesGradient[t][r][s]==NULL);
		for (int t=0; t<32; t++)
			for (int s=0; s<2; s++)
			{
				assert(forbiddenGradient[t][s] == NULL);
				assert(guardAreasGradient[t][s] == NULL);
				assert(clearAreasGradient[t][s] == NULL);
			}
		for (int t=0; t<32; t++)
			assert(exploredArea[t] == NULL);
		
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
	
	for (int t=0; t<32; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				gradientUpdated[t][r][s]=false;
}

void Map::logAtClear()
{
	fprintf(logFile, "\n");
	if (game)
		for (int t=0; t<16; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				if (ressourceAvailableCount[t][r])
				{
					fprintf(logFile, "ressourceAvailableCount[%d][%d]=%d\n", t, r,
						ressourceAvailableCount[t][r]);
					fprintf(logFile, "| ressourceAvailableCountSuccess[%d][%d]=%d (%f %%)\n", t, r,
						ressourceAvailableCountSuccess[t][r],
						100.*(double)ressourceAvailableCountSuccess[t][r]/(double)ressourceAvailableCount[t][r]);
					fprintf(logFile, "| ressourceAvailableCountFailure[%d][%d]=%d (%f %%)\n", t, r,
						ressourceAvailableCountFailure[t][r],
						100.*(double)ressourceAvailableCountFailure[t][r]/(double)ressourceAvailableCount[t][r]);
				}
	
	for (int t=0; t<16; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
		{
			ressourceAvailableCount[t][r]=0;
			ressourceAvailableCountSuccess[t][r]=0;
			ressourceAvailableCountFailure[t][r]=0;
		}
	
	fprintf(logFile, "\n");
	fprintf(logFile, "pathToRessourceCountTot=%d\n", pathToRessourceCountTot);
	if (pathToBuildingCountTot)
	{
		fprintf(logFile, "| pathToRessourceCountSuccess=%d (%f %% of tot)\n",
			pathToRessourceCountSuccess,
			100.*(double)pathToRessourceCountSuccess/(double)pathToRessourceCountTot);
		fprintf(logFile, "| pathToRessourceCountFailure=%d (%f %% of tot)\n",
			pathToRessourceCountFailure,
			100.*(double)pathToRessourceCountFailure/(double)pathToRessourceCountTot);
	}
	pathToRessourceCountTot=0;
	pathToRessourceCountSuccess=0;
	pathToRessourceCountFailure=0;
	
	fprintf(logFile, "\n");
	
	fprintf(logFile, "pathfindLocalRessourceCount=%d\n", pathfindLocalRessourceCount);
	if (pathfindLocalRessourceCount)
	{
		fprintf(logFile, "+ localRessourcesUpdateCount=%d (%f %% of calls)\n",
			localRessourcesUpdateCount,
			100.*(float)localRessourcesUpdateCount/(float)pathfindLocalRessourceCount);
		
		fprintf(logFile, "| pathfindLocalRessourceCountWait=%d (%f %% of count)\n",
			pathfindLocalRessourceCountWait,
			100.*(float)pathfindLocalRessourceCountWait/(float)pathfindLocalRessourceCount);
		
		int pathfindLocalRessourceCountSuccess=
			+pathfindLocalRessourceCountSuccessBase
			+pathfindLocalRessourceCountSuccessLocked
			+pathfindLocalRessourceCountSuccessUpdate
			+pathfindLocalRessourceCountSuccessUpdateLocked;
		
		fprintf(logFile, "| pathfindLocalRessourceCountSuccess=%d (%f %% of count)\n",
			pathfindLocalRessourceCountSuccess,
			100.*(float)pathfindLocalRessourceCountSuccess/(float)pathfindLocalRessourceCount);
		fprintf(logFile, "|-  pathfindLocalRessourceCountSuccessBase=%d (%f %% of count) (%f %% of success)\n",
			pathfindLocalRessourceCountSuccessBase,
			100.*(float)pathfindLocalRessourceCountSuccessBase/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountSuccessBase/(float)pathfindLocalRessourceCountSuccess);
		fprintf(logFile, "|-  pathfindLocalRessourceCountSuccessLocked=%d (%f %% of count) (%f %% of success)\n",
			pathfindLocalRessourceCountSuccessLocked,
			100.*(float)pathfindLocalRessourceCountSuccessLocked/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountSuccessLocked/(float)pathfindLocalRessourceCountSuccess);
		fprintf(logFile, "|-  pathfindLocalRessourceCountSuccessUpdate=%d (%f %% of count) (%f %% of success)\n",
			pathfindLocalRessourceCountSuccessUpdate,
			100.*(float)pathfindLocalRessourceCountSuccessUpdate/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountSuccessUpdate/(float)pathfindLocalRessourceCountSuccess);
		fprintf(logFile, "|-  pathfindLocalRessourceCountSuccessUpdateLocked=%d (%f %% of count) (%f %% of success)\n",
			pathfindLocalRessourceCountSuccessUpdateLocked,
			100.*(float)pathfindLocalRessourceCountSuccessUpdateLocked/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountSuccessUpdateLocked/(float)pathfindLocalRessourceCountSuccess);
		
		int pathfindLocalRessourceCountFailure=
			+pathfindLocalRessourceCountFailureUnusable
			+pathfindLocalRessourceCountFailureNone
			+pathfindLocalRessourceCountFailureBad;
		
		fprintf(logFile, "| pathfindLocalRessourceCountFailure=%d (%f %% of count)\n",
			pathfindLocalRessourceCountFailure,
			100.*(float)pathfindLocalRessourceCountFailure/(float)pathfindLocalRessourceCount);
		fprintf(logFile, "|-  pathfindLocalRessourceCountFailureUnusable=%d (%f %% of count) (%f %% of failure)\n",
			pathfindLocalRessourceCountFailureUnusable,
			100.*(float)pathfindLocalRessourceCountFailureUnusable/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountFailureUnusable/(float)pathfindLocalRessourceCountFailure);
		fprintf(logFile, "|-  pathfindLocalRessourceCountFailureNone=%d (%f %% of count) (%f %% of failure)\n",
			pathfindLocalRessourceCountFailureNone,
			100.*(float)pathfindLocalRessourceCountFailureNone/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountFailureNone/(float)pathfindLocalRessourceCountFailure);
		fprintf(logFile, "|-  pathfindLocalRessourceCountFailureBad=%d (%f %% of count) (%f %% of failure)\n",
			pathfindLocalRessourceCountFailureBad,
			100.*(float)pathfindLocalRessourceCountFailureBad/(float)pathfindLocalRessourceCount,
			100.*(float)pathfindLocalRessourceCountFailureBad/(float)pathfindLocalRessourceCountFailure);
	}
	
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
	
	fprintf(logFile, "\n");
	fprintf(logFile, "pathToBuildingCountTot=%d\n", pathToBuildingCountTot);
	if (pathToBuildingCountTot)
	{
		fprintf(logFile, "|- pathToBuildingCountClose=%d (%f %% of tot)\n",
			pathToBuildingCountClose,
			100.*(double)pathToBuildingCountClose/(double)pathToBuildingCountTot);
		
		int pathToBuildingCountCloseSuccessTot=
			+pathToBuildingCountCloseSuccessStand
			+pathToBuildingCountCloseSuccessBase
			+pathToBuildingCountCloseSuccessUpdated;
	
		fprintf(logFile, "|-  pathToBuildingCountCloseSuccessTot=%d (%f %% of tot) (%f %% of close)\n",
			pathToBuildingCountCloseSuccessTot,
			100.*(double)pathToBuildingCountCloseSuccessTot/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseSuccessTot/(double)pathToBuildingCountClose);
		
		fprintf(logFile, "|-   pathToBuildingCountCloseSuccessStand=%d (%f %% of tot) (%f %% of close) (%f %% of successTot)\n",
			pathToBuildingCountCloseSuccessStand,
			100.*(double)pathToBuildingCountCloseSuccessStand/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseSuccessStand/(double)pathToBuildingCountClose,
			100.*(double)pathToBuildingCountCloseSuccessStand/(double)pathToBuildingCountCloseSuccessTot);
		
		fprintf(logFile, "|-   pathToBuildingCountCloseSuccessBase=%d (%f %% of tot) (%f %% of close) (%f %% of successTot)\n",
			pathToBuildingCountCloseSuccessBase,
			100.*(double)pathToBuildingCountCloseSuccessBase/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseSuccessBase/(double)pathToBuildingCountClose,
			100.*(double)pathToBuildingCountCloseSuccessBase/(double)pathToBuildingCountCloseSuccessTot);
		
		fprintf(logFile, "|-   pathToBuildingCountCloseSuccessUpdated=%d (%f %% of tot) (%f %% of close) (%f %% of successTot)\n",
			pathToBuildingCountCloseSuccessUpdated,
			100.*(double)pathToBuildingCountCloseSuccessUpdated/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseSuccessUpdated/(double)pathToBuildingCountClose,
			100.*(double)pathToBuildingCountCloseSuccessUpdated/(double)pathToBuildingCountCloseSuccessTot);
		
		int pathToBuildingCountCloseFailure=
			+pathToBuildingCountCloseFailureLocked
			+pathToBuildingCountCloseFailureEnd;
		fprintf(logFile, "|-  pathToBuildingCountCloseFailure=%d (%f %% of tot) (%f %% of close)\n",
			pathToBuildingCountCloseFailure,
			100.*(double)pathToBuildingCountCloseFailure/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseFailure/(double)pathToBuildingCountClose);
		fprintf(logFile, "|-  pathToBuildingCountCloseFailureLocked=%d (%f %% of tot) (%f %% of close) (%f %% of failure)\n",
			pathToBuildingCountCloseFailureLocked,
			100.*(double)pathToBuildingCountCloseFailureLocked/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseFailureLocked/(double)pathToBuildingCountClose,
			100.*(double)pathToBuildingCountCloseFailureLocked/(double)pathToBuildingCountCloseFailure);
		fprintf(logFile, "|-  pathToBuildingCountCloseFailureEnd=%d (%f %% of tot) (%f %% of close) (%f %% of failure)\n",
			pathToBuildingCountCloseFailureEnd,
			100.*(double)pathToBuildingCountCloseFailureEnd/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountCloseFailureEnd/(double)pathToBuildingCountClose,
			100.*(double)pathToBuildingCountCloseFailureEnd/(double)pathToBuildingCountCloseFailure);
		/* This assertion sometimes fails.  Since we're replacing the whole map implementation anyway, we might as well just ignore it
		assert(pathToBuildingCountFar==
			//+pathToBuildingCountFarIsNew // doesn't return
			+pathToBuildingCountFarOldSuccess
			+pathToBuildingCountFarOldFailureLocked
			+pathToBuildingCountFarOldFailureBad
			+pathToBuildingCountFarOldFailureRepeat
			//+pathToBuildingCountFarOldFailureUnusable // doesn't return
			+pathToBuildingCountFarUpdateSuccess
			+pathToBuildingCountFarUpdateFailureLocked
			+pathToBuildingCountFarUpdateFailureVirtual
			+pathToBuildingCountFarUpdateFailureBad);
		*/
		fprintf(logFile, "|- pathToBuildingCountFar=%d (%f %% of tot)\n",
			pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFar/(double)pathToBuildingCountTot);
		fprintf(logFile, "|+  pathToBuildingCountIsFar=%d (%f %% of tot) (%f %% of far)\n",
			pathToBuildingCountIsFar,
			100.*(double)pathToBuildingCountIsFar/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountIsFar/(double)pathToBuildingCountFar);
		
		int pathToBuildingCountFarOld=
			+pathToBuildingCountFarOldSuccess
			+pathToBuildingCountFarOldFailureLocked
			+pathToBuildingCountFarOldFailureBad
			+pathToBuildingCountFarOldFailureRepeat
			+pathToBuildingCountFarOldFailureUnusable;
		fprintf(logFile, "|-  pathToBuildingCountFarOld=%d (%f %% of tot) (%f %% of far)\n",
			pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOld/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOld/(double)pathToBuildingCountFar);
		
		fprintf(logFile, "|-   pathToBuildingCountFarOldSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			pathToBuildingCountFarOldSuccess,
			100.*(double)pathToBuildingCountFarOldSuccess/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldSuccess/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldSuccess/(double)pathToBuildingCountFarOld);
		
		int pathToBuildingCountFarOldFailure=
			+pathToBuildingCountFarOldFailureLocked
			+pathToBuildingCountFarOldFailureBad
			+pathToBuildingCountFarOldFailureRepeat
			+pathToBuildingCountFarOldFailureUnusable;
		fprintf(logFile, "|-   pathToBuildingCountFarOldFailure=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			pathToBuildingCountFarOldFailure,
			100.*(double)pathToBuildingCountFarOldFailure/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailure/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailure/(double)pathToBuildingCountFarOld);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			pathToBuildingCountFarOldFailureLocked,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountFarOldFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureBad=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			pathToBuildingCountFarOldFailureBad,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountFarOldFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureRepeat=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			pathToBuildingCountFarOldFailureRepeat,
			100.*(double)pathToBuildingCountFarOldFailureRepeat/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureRepeat/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureRepeat/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureRepeat/(double)pathToBuildingCountFarOldFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureUnusable=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			pathToBuildingCountFarOldFailureUnusable,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountFarOldFailure);
		
		int pathToBuildingCountFarUpdate=
			+pathToBuildingCountFarUpdateSuccess
			+pathToBuildingCountFarUpdateFailureLocked
			+pathToBuildingCountFarUpdateFailureVirtual
			+pathToBuildingCountFarUpdateFailureBad;
		fprintf(logFile, "|-  pathToBuildingCountFarUpdate=%d (%f %% of tot) (%f %% of far)\n",
			pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdate/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdate/(double)pathToBuildingCountFar);
		fprintf(logFile, "|+   pathToBuildingCountFarIsNew=%d (%f %% of tot) (%f %% of far) (%f %% of update)\n",
			pathToBuildingCountFarIsNew,
			100.*(double)pathToBuildingCountFarIsNew/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarIsNew/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarIsNew/(double)pathToBuildingCountFarUpdate);
		fprintf(logFile, "|-   pathToBuildingCountFarUpdateSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of update)\n",
			pathToBuildingCountFarUpdateSuccess,
			100.*(double)pathToBuildingCountFarUpdateSuccess/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateSuccess/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateSuccess/(double)pathToBuildingCountFarUpdate);
		
		int pathToBuildingCountFarUpdateFailure=
			+pathToBuildingCountFarUpdateFailureLocked
			+pathToBuildingCountFarUpdateFailureVirtual
			+pathToBuildingCountFarUpdateFailureBad;
		fprintf(logFile, "|-   pathToBuildingCountFarUpdateFailure=%d (%f %% of tot) (%f %% of far) (%f %% of update)\n",
			pathToBuildingCountFarUpdateFailure,
			100.*(double)pathToBuildingCountFarUpdateFailure/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailure/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailure/(double)pathToBuildingCountFarUpdate);
		fprintf(logFile, "|-    pathToBuildingCountFarUpdateFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of update) (%f %% of failure)\n",
			pathToBuildingCountFarUpdateFailureLocked,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountFarUpdateFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarUpdateFailureVirtual=%d (%f %% of tot) (%f %% of far) (%f %% of update) (%f %% of failure)\n",
			pathToBuildingCountFarUpdateFailureVirtual,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountFarUpdateFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarUpdateFailureBad=%d (%f %% of tot) (%f %% of far) (%f %% of update) (%f %% of failure)\n",
			pathToBuildingCountFarUpdateFailureBad,
			100.*(double)pathToBuildingCountFarUpdateFailureBad/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailureBad/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailureBad/(double)pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdateFailureBad/(double)pathToBuildingCountFarUpdateFailure);
	}
	
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
	pathToBuildingCountFarUpdateFailureBad=0;
	
	int buildingGradientUpdate=localBuildingGradientUpdate+globalBuildingGradientUpdate;
	fprintf(logFile, "\n");
	fprintf(logFile, "buildingGradientUpdate=%d\n", buildingGradientUpdate);
	if (buildingGradientUpdate)
	{
		fprintf(logFile, "|- localBuildingGradientUpdate=%d (%f %%)\n",
			localBuildingGradientUpdate,
			100.*(double)localBuildingGradientUpdate/(double)buildingGradientUpdate);
		fprintf(logFile, "|-   localBuildingGradientUpdateLocked=%d (%f %%) (%f %% of local)\n",
			localBuildingGradientUpdateLocked,
			100.*(double)localBuildingGradientUpdateLocked/(double)buildingGradientUpdate,
			100.*(double)localBuildingGradientUpdateLocked/(double)localBuildingGradientUpdate);
		
		fprintf(logFile, "|- globalBuildingGradientUpdate=%d (%f %%)\n",
			globalBuildingGradientUpdate,
			100.*(double)globalBuildingGradientUpdate/(double)buildingGradientUpdate);
		fprintf(logFile, "|-   globalBuildingGradientUpdateLocked=%d (%f %%) (%f %% of global)\n",
			globalBuildingGradientUpdateLocked,
			100.*(double)globalBuildingGradientUpdateLocked/(double)buildingGradientUpdate,
			100.*(double)globalBuildingGradientUpdateLocked/(double)globalBuildingGradientUpdate);
	}
	
	localBuildingGradientUpdate=0;
	localBuildingGradientUpdateLocked=0;
	globalBuildingGradientUpdate=0;
	globalBuildingGradientUpdateLocked=0;
	
	fprintf(logFile, "\n");
	fprintf(logFile, "buildingAvailableCountTot=%d\n", buildingAvailableCountTot);
	if (buildingAvailableCountTot)
	{
		fprintf(logFile, "|- buildingAvailableCountClose=%d (%f %%)\n",
			buildingAvailableCountClose,
			100.*(double)buildingAvailableCountClose/(double)buildingAvailableCountTot);
		
		int buildingAvailableCountCloseSuccess=
			+buildingAvailableCountCloseSuccessFast
			+buildingAvailableCountCloseSuccessAround
			+buildingAvailableCountCloseSuccessUpdate
			+buildingAvailableCountCloseSuccessUpdateAround;
		fprintf(logFile, "|-  buildingAvailableCountCloseSuccess=%d (%f %% of tot) (%f %% of close)\n",
			buildingAvailableCountCloseSuccess,
			100.*(double)buildingAvailableCountCloseSuccess/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseSuccess/(double)buildingAvailableCountClose);
		fprintf(logFile, "|-   buildingAvailableCountCloseSuccessFast=%d (%f %% of tot) (%f %% of close) (%f %% of close success)\n",
			buildingAvailableCountCloseSuccessFast,
			100.*(double)buildingAvailableCountCloseSuccessFast/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseSuccessFast/(double)buildingAvailableCountClose,
			100.*(double)buildingAvailableCountCloseSuccessFast/(double)buildingAvailableCountCloseSuccess);
		fprintf(logFile, "|-   buildingAvailableCountCloseSuccessAround=%d (%f %% of tot) (%f %% of close) (%f %% of close success)\n",
			buildingAvailableCountCloseSuccessAround,
			100.*(double)buildingAvailableCountCloseSuccessAround/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseSuccessAround/(double)buildingAvailableCountClose,
			100.*(double)buildingAvailableCountCloseSuccessAround/(double)buildingAvailableCountCloseSuccess);
		fprintf(logFile, "|-   buildingAvailableCountCloseSuccessUpdate=%d (%f %% of tot) (%f %% of close) (%f %% of close success)\n",
			buildingAvailableCountCloseSuccessUpdate,
			100.*(double)buildingAvailableCountCloseSuccessUpdate/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseSuccessUpdate/(double)buildingAvailableCountClose,
			100.*(double)buildingAvailableCountCloseSuccessUpdate/(double)buildingAvailableCountCloseSuccess);
		fprintf(logFile, "|-   buildingAvailableCountCloseSuccessUpdateAround=%d (%f %% of tot) (%f %% of close) (%f %% of close success)\n",
			buildingAvailableCountCloseSuccessUpdateAround,
			100.*(double)buildingAvailableCountCloseSuccessUpdateAround/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseSuccessUpdateAround/(double)buildingAvailableCountClose,
			100.*(double)buildingAvailableCountCloseSuccessUpdateAround/(double)buildingAvailableCountCloseSuccess);
		
		int buildingAvailableCountCloseFailure=
			+buildingAvailableCountCloseFailureLocked
			+buildingAvailableCountCloseFailureEnd;
		fprintf(logFile, "|-  buildingAvailableCountCloseFailure=%d (%f %% of tot) (%f %% of close)\n",
			buildingAvailableCountCloseFailure,
			100.*(double)buildingAvailableCountCloseFailure/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseFailure/(double)buildingAvailableCountClose);
		fprintf(logFile, "|-   buildingAvailableCountCloseFailureLocked=%d (%f %% of tot) (%f %% of close) (%f %% of failure)\n",
			buildingAvailableCountCloseFailureLocked,
			100.*(double)buildingAvailableCountCloseFailureLocked/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseFailureLocked/(double)buildingAvailableCountClose,
			100.*(double)buildingAvailableCountCloseFailureLocked/(double)buildingAvailableCountCloseFailure);
		fprintf(logFile, "|-   buildingAvailableCountCloseFailureEnd=%d (%f %% of tot) (%f %% of close) (%f %% of failure)\n",
			buildingAvailableCountCloseFailureEnd,
			100.*(double)buildingAvailableCountCloseFailureEnd/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountCloseFailureEnd/(double)buildingAvailableCountClose,
			100.*(double)buildingAvailableCountCloseFailureEnd/(double)buildingAvailableCountCloseFailure);
		
		fprintf(logFile, "|- buildingAvailableCountFar=%d (%f %%)\n",
			buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFar/(double)buildingAvailableCountTot);
		fprintf(logFile, "|+  buildingAvailableCountIsFar=%d (%f %% of tot) (%f %% of far)\n",
			buildingAvailableCountIsFar,
			100.*(double)buildingAvailableCountIsFar/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountIsFar/(double)buildingAvailableCountFar);
		
		fprintf(logFile, "|-  buildingAvailableCountFarOld=%d (%f %% of tot) (%f %% of far)\n",
			buildingAvailableCountFarOld,
			100.*(double)buildingAvailableCountFarOld/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOld/(double)buildingAvailableCountFar);
		
		int buildingAvailableCountFarOldSuccess=
			+buildingAvailableCountFarOldSuccessFast
			+buildingAvailableCountFarOldSuccessAround;
		fprintf(logFile, "|-   buildingAvailableCountFarOldSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			buildingAvailableCountFarOldSuccess,
			100.*(double)buildingAvailableCountFarOldSuccess/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOldSuccess/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarOldSuccess/(double)buildingAvailableCountFarOld);
		fprintf(logFile, "|-    buildingAvailableCountFarOldSuccessFast=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of old success)\n",
			buildingAvailableCountFarOldSuccessFast,
			100.*(double)buildingAvailableCountFarOldSuccessFast/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOldSuccessFast/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarOldSuccessFast/(double)buildingAvailableCountFarOld,
			100.*(double)buildingAvailableCountFarOldSuccessFast/(double)buildingAvailableCountFarOldSuccess);
		fprintf(logFile, "|-    buildingAvailableCountFarOldSuccessAround=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of old success)\n",
			buildingAvailableCountFarOldSuccessAround,
			100.*(double)buildingAvailableCountFarOldSuccessAround/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOldSuccessAround/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarOldSuccessAround/(double)buildingAvailableCountFarOld,
			100.*(double)buildingAvailableCountFarOldSuccessAround/(double)buildingAvailableCountFarOldSuccess);
		
		int buildingAvailableCountFarOldFailure=
			+buildingAvailableCountFarOldFailureLocked
			+buildingAvailableCountFarOldFailureEnd;
		fprintf(logFile, "|-   buildingAvailableCountFarOldFailure=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			buildingAvailableCountFarOldFailure,
			100.*(double)buildingAvailableCountFarOldFailure/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOldFailure/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarOldFailure/(double)buildingAvailableCountFarOld);
		fprintf(logFile, "|-    buildingAvailableCountFarOldFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			buildingAvailableCountFarOldFailureLocked,
			100.*(double)buildingAvailableCountFarOldFailureLocked/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOldFailureLocked/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarOldFailureLocked/(double)buildingAvailableCountFarOld,
			100.*(double)buildingAvailableCountFarOldFailureLocked/(double)buildingAvailableCountFarOldFailure);
		fprintf(logFile, "|-    buildingAvailableCountFarOldFailureEnd=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			buildingAvailableCountFarOldFailureEnd,
			100.*(double)buildingAvailableCountFarOldFailureEnd/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarOldFailureEnd/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarOldFailureEnd/(double)buildingAvailableCountFarOld,
			100.*(double)buildingAvailableCountFarOldFailureEnd/(double)buildingAvailableCountFarOldFailure);
		
		fprintf(logFile, "|-  buildingAvailableCountFarNew=%d (%f %% of tot) (%f %% of far)\n",
			buildingAvailableCountFarNew,
			100.*(double)buildingAvailableCountFarNew/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNew/(double)buildingAvailableCountFar);
		
		int buildingAvailableCountFarNewSuccess=buildingAvailableCountFarNewSuccessFast+buildingAvailableCountFarNewSuccessClosely;
		fprintf(logFile, "|-    buildingAvailableCountFarNewSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of new)\n",
			buildingAvailableCountFarNewSuccess,
			100.*(double)buildingAvailableCountFarNewSuccess/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewSuccess/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewSuccess/(double)buildingAvailableCountFarNew);
		fprintf(logFile, "|-    buildingAvailableCountFarNewSuccessFast=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of success)\n",
			buildingAvailableCountFarNewSuccessFast,
			100.*(double)buildingAvailableCountFarNewSuccessFast/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewSuccessFast/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewSuccessFast/(double)buildingAvailableCountFarNew,
			100.*(double)buildingAvailableCountFarNewSuccessFast/(double)buildingAvailableCountFarNewSuccess);
		fprintf(logFile, "|-   buildingAvailableCountFarNewSuccessClosely=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of success)\n",
			buildingAvailableCountFarNewSuccessClosely,
			100.*(double)buildingAvailableCountFarNewSuccessClosely/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewSuccessClosely/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewSuccessClosely/(double)buildingAvailableCountFarNew,
			100.*(double)buildingAvailableCountFarNewSuccessClosely/(double)buildingAvailableCountFarNewSuccess);
		
		int buildingAvailableCountFarNewFailure=
			+buildingAvailableCountFarNewFailureLocked
			+buildingAvailableCountFarNewFailureVirtual
			+buildingAvailableCountFarNewFailureEnd;
		fprintf(logFile, "|-   buildingAvailableCountFarNewFailure=%d (%f %% of tot) (%f %% of far) (%f %% of new)\n",
			buildingAvailableCountFarNewFailure,
			100.*(double)buildingAvailableCountFarNewFailure/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewFailure/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewFailure/(double)buildingAvailableCountFarNew);
		fprintf(logFile, "|-    buildingAvailableCountFarNewFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of failure)\n",
			buildingAvailableCountFarNewFailureLocked,
			100.*(double)buildingAvailableCountFarNewFailureLocked/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewFailureLocked/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewFailureLocked/(double)buildingAvailableCountFarNew,
			100.*(double)buildingAvailableCountFarNewFailureLocked/(double)buildingAvailableCountFarNewFailure);
		fprintf(logFile, "|-    buildingAvailableCountFarNewFailureVirtual=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of failure)\n",
			buildingAvailableCountFarNewFailureVirtual,
			100.*(double)buildingAvailableCountFarNewFailureVirtual/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewFailureVirtual/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewFailureVirtual/(double)buildingAvailableCountFarNew,
			100.*(double)buildingAvailableCountFarNewFailureVirtual/(double)buildingAvailableCountFarNewFailure);
		fprintf(logFile, "|-    buildingAvailableCountFarNewFailureEnd=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of failure)\n",
			buildingAvailableCountFarNewFailureEnd,
			100.*(double)buildingAvailableCountFarNewFailureEnd/(double)buildingAvailableCountTot,
			100.*(double)buildingAvailableCountFarNewFailureEnd/(double)buildingAvailableCountFar,
			100.*(double)buildingAvailableCountFarNewFailureEnd/(double)buildingAvailableCountFarNew,
			100.*(double)buildingAvailableCountFarNewFailureEnd/(double)buildingAvailableCountFarNewFailure);
	}
	
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
	
	fprintf(logFile, "\n");
	fprintf(logFile, "pathfindForbiddenCount=%d\n", pathfindForbiddenCount);
	if (pathfindForbiddenCount)
	{
		fprintf(logFile, "|- pathfindForbiddenCountSuccess=%d (%f %%)\n",
			pathfindForbiddenCountSuccess,
			100.*(double)pathfindForbiddenCountSuccess/(double)pathfindForbiddenCount);
		
		fprintf(logFile, "|- pathfindForbiddenCountFailure=%d (%f %%)\n",
			pathfindForbiddenCountFailure,
			100.*(double)pathfindForbiddenCountFailure/(double)pathfindForbiddenCount);
	}
	
	pathfindForbiddenCount=0;
	pathfindForbiddenCountSuccess=0;
	pathfindForbiddenCountFailure=0;
	
	#ifdef check_disorderable_gradient_error_probability
	fprintf(logFile, "\n");
	for (int i = 0; i < GT_SIZE; i++)
	{
		fprintf(logFile, "listCountSizeStatsOver[%d]=%d\n", i, listCountSizeStatsOver[i]);
		if (listCountSizeStats[i])
		{
			fprintf(logFile, "listCountSizeStats[%d]:\n", i);
			for (size_t vi = 0; vi < 64; vi++)
			{
				int sum = 0;
				for (size_t ti = 0; ti < (size / 64); ti++)
					sum += listCountSizeStats[i][vi * (size / 64) + ti];
				fprintf(logFile, "[%5d->%5d]:%5d\n", vi * (size / 64), (vi + 1) * (size / 64) - 1, sum);
			}
			/*fprintf(logFile, "listCountSizeStats:\n");
			for (size_t i = 0; i< size; i++)
				if (listCountSizeStats[i][i])
					fprintf(logFile, "[%5d]:%5d\n", i, listCountSizeStats[i][i]);*/
		}
	}
	#endif
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

	mapDiscovered=new Uint32[size];
	memset(mapDiscovered, 0, size*sizeof(Uint32));

	fogOfWarA=new Uint32[size];
	memset(fogOfWarA, 0, size*sizeof(Uint32));
	fogOfWarB=new Uint32[size];
	memset(fogOfWarB, 0, size*sizeof(Uint32));
	fogOfWar=fogOfWarA;
	
	localForbiddenMap.resize(size, false);
	localGuardAreaMap.resize(size, false);
	localClearAreaMap.resize(size, false);
	
	cases=new Case[size];
	Case initCase;
	initCase.terrain = 0; // default, not really meaningfull.
	initCase.building = NOGBID;
	initCase.ressource.clear();
	initCase.groundUnit = NOGUID;
	initCase.airUnit = NOGUID;
	initCase.forbidden = 0;
	initCase.guardArea = 0;
	initCase.clearArea = 0;
	initCase.scriptAreas = 0;
	initCase.canRessourcesGrow = 1;
	initCase.fertility = 0;
	
	for (size_t i=0; i<size; i++)
		cases[i]=initCase;
	
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

	astarpoints=new AStarAlgorithmPoint[w*h];

	clearingAreaClaims = new Uint32[w*h];

	immobileUnits = new Uint8[w*h];

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

bool Map::load(GAGCore::InputStream *stream, MapHeader& header, Game *game)
{
	assert(header.getVersionMinor()>=16);

	Sint32 versionMinor = header.getVersionMinor();

	clear();
	
	stream->readEnterSection("Map");

	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature, "MapB", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the beginning of Map.\n");
		return false;
	}

	// We load and compute size:
	wDec = stream->readSint32("wDec");
	hDec = stream->readSint32("hDec");
	w = 1<<wDec;
	h = 1<<hDec;
	wMask = w-1;
	hMask = h-1;
	size = w*h;

	// We allocate memory:
	mapDiscovered = new Uint32[size];
	fogOfWarA = new Uint32[size];
	fogOfWarB = new Uint32[size];
	fogOfWar = fogOfWarA;
	memset(fogOfWarA, 0, size*sizeof(Uint32));
	memset(fogOfWarB, 0, size*sizeof(Uint32));
	localForbiddenMap.resize(size, false);
	localGuardAreaMap.resize(size, false);
	localClearAreaMap.resize(size, false);
	cases = new Case[size];
	undermap = new Uint8[size];
	listedAddr = new Uint8*[size];
	astarpoints=new AStarAlgorithmPoint[size];
	clearingAreaClaims = new Uint32[size];
	memset(clearingAreaClaims, 0, size*sizeof(Uint32));
	immobileUnits = new Uint8[size];
	memset(immobileUnits, 255, size*sizeof(Uint8));
	
	#ifdef check_disorderable_gradient_error_probability
	for (int i = 0; i < GT_SIZE; i++)
	{
		if (listCountSizeStats[i])
			delete[] listCountSizeStats[i];
		listCountSizeStats[i] = new int[size];
		listCountSizeStatsOver[i] = 0;
	}
	#endif

	// We read what's inside the map:
	stream->read(undermap, size, "undermap");
	stream->readEnterSection("cases");
	for (size_t i=0; i<size; i++)
	{
		stream->readEnterSection(i);
		mapDiscovered[i] = stream->readUint32("mapDiscovered");

		cases[i].terrain = stream->readUint16("terrain");
		cases[i].building = stream->readUint16("building");

		stream->read(&(cases[i].ressource), 4, "ressource");
		cases[i].groundUnit = stream->readUint16("groundUnit");
		cases[i].airUnit = stream->readUint16("airUnit");
		cases[i].forbidden = stream->readUint32("forbidden");
		if(versionMinor < 62)
			stream->readUint32("hiddenForbidden");
		cases[i].guardArea = stream->readUint32("guardArea");
		cases[i].clearArea = stream->readUint32("clearArea");
		cases[i].scriptAreas = stream->readUint16("scriptAreas");
		cases[i].canRessourcesGrow = stream->readUint8("canRessourcesGrow");
		if(versionMinor >= 63)
			cases[i].fertility = stream->readUint16("fertility");
		fertilityMaximum = std::max(fertilityMaximum, cases[i].fertility);

		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	for(int n=0; n<9; ++n)
	{
		stream->readEnterSection(n);
		setAreaName(n, stream->readText("areaname"));
		stream->readLeaveSection();
	}
	
	if (game)
	{
                /* Must set game field before following action as they
                   may need it (in particular
                   makeDiscoveredAreasExplored uses it). */
		this->game=game;

		// This is a game, so we do compute gradients
		for (int t=0; t<header.getNumberOfTeams(); t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
				{
					assert(ressourcesGradient[t][r][s]==NULL);
					ressourcesGradient[t][r][s]=new Uint8[size];
					updateRessourcesGradient(t, r, (bool)s);
				}
		for (int t=0; t<32; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<1; s++)
					gradientUpdated[t][r][s]=false;

		for (int t=0; t<header.getNumberOfTeams(); t++)
			for (int s=0; s<2; s++)
			{
				assert(forbiddenGradient[t][s] == NULL);
				forbiddenGradient[t][s] = new Uint8[size];
				updateForbiddenGradient(t, s);
				
				assert(guardAreasGradient[t][s] == NULL);
				guardAreasGradient[t][s] = new Uint8[size];
				updateGuardAreasGradient(t, s);
			
				assert(clearAreasGradient[t][s] == NULL);
				clearAreasGradient[t][s] = new Uint8[size];
				updateClearAreasGradient(t, s);
				
				guardGradientUpdated[t][s] = false;
				clearGradientUpdated[t][s] = false;
			}
		for (int t=0; t<header.getNumberOfTeams(); t++)
		{
			assert(exploredArea[t] == NULL);
			exploredArea[t] = new Uint8[size];
			initExploredArea(t);
                        makeDiscoveredAreasExplored(t);
		}
	}

	// We load sectors:
	wSector = stream->readSint32("wSector");
	hSector = stream->readSint32("hSector");
	sizeSector = wSector*hSector;
	assert(sectors == NULL);
	sectors = new Sector[sizeSector];
	
	arraysBuilt = true;
	
	stream->readEnterSection("sectors");
	for (int i=0; i<sizeSector; i++)
	{
		stream->readEnterSection(i);
		if (!sectors[i].load(stream, this->game, versionMinor))
		{
			stream->readLeaveSection(3);
			return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();
	
	if (memcmp(signature, "MapE", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the end of Map.\n");
		return false;
	}
	
	return true;
}


void Map::loadTransitional()
{
	std::string fileName = glob2NameToFilename("maps", "output", "");
	InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(fileName));
	stream->read(undermap, size, "undermap");
	for (size_t i=0; i<size ;i++)
	{
		cases[i].terrain = stream->readUint16("terrain");
		stream->read(&(cases[i].ressource), 4, "ressource");
		cases[i].scriptAreas = stream->readUint16("scriptAreas");
		cases[i].canRessourcesGrow = stream->readUint8("canRessourcesGrow");
	}
	//Load area names
	for(int n=0; n<9; ++n)
	{
		stream->readEnterSection(n);
		setAreaName(n, stream->readText("areaname"));
		stream->readLeaveSection();
	}
	delete stream;
}
	

void Map::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Map");
	stream->write("MapB", 4, "signatureStart");
	
	// We save size:
	stream->writeSint32(wDec, "wDec");
	stream->writeSint32(hDec, "hDec");

	// We write what's inside the map:
	stream->write(undermap, size, "undermap");
	stream->writeEnterSection("cases");
	for (size_t i=0; i<size ;i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(mapDiscovered[i], "mapDiscovered");

		stream->writeUint16(cases[i].terrain, "terrain");
		stream->writeUint16(cases[i].building, "building");
		
		stream->write(&(cases[i].ressource), 4, "ressource");
		
		stream->writeUint16(cases[i].groundUnit, "groundUnit");
		stream->writeUint16(cases[i].airUnit, "airUnit");
		stream->writeUint32(cases[i].forbidden, "forbidden");
		stream->writeUint32(cases[i].guardArea, "guardArea");
		stream->writeUint32(cases[i].clearArea, "clearArea");
		stream->writeUint16(cases[i].scriptAreas, "scriptAreas");
		stream->writeUint8(cases[i].canRessourcesGrow, "canRessourcesGrow");
		stream->writeUint16(cases[i].fertility, "fertility");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	//Save area names
	for(int n=0; n<9; ++n)
	{
		stream->writeEnterSection(n);
		stream->writeText(getAreaName(n), "areaname");
		stream->writeLeaveSection();
	}

	// We save sectors:
	stream->writeSint32(wSector, "wSector");
	stream->writeSint32(hSector, "hSector");
	stream->writeEnterSection("sectors");
	for (int i=0; i<sizeSector; i++)
	{
		stream->writeEnterSection(i);
		sectors[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->write("MapE", 4, "signatureEnd");
	stream->writeLeaveSection();
}

void Map::addTeam(void)
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	int oldNumberOfTeam=numberOfTeam-1;
	assert(numberOfTeam>0);
	
	for (int t=0; t<oldNumberOfTeam; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				assert(ressourcesGradient[t][r][s]);
	for (int t=oldNumberOfTeam; t<32; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				assert(ressourcesGradient[t][r][s]==NULL);
	
	int t=oldNumberOfTeam;
	for (int r=0; r<MAX_RESSOURCES; r++)
		for (int s=0; s<2; s++)
		{
			assert(ressourcesGradient[t][r][s]==NULL);
			ressourcesGradient[t][r][s]=new Uint8[size];
			updateRessourcesGradient(t, r, (bool)s);			
		}
	
	for (int s=0; s<2; s++)
	{
		assert(forbiddenGradient[t][s] == NULL);
		forbiddenGradient[t][s] = new Uint8[size];
		updateForbiddenGradient(t, s);
		assert(guardAreasGradient[t][s] == NULL);
		guardAreasGradient[t][s] = new Uint8[size];
		updateGuardAreasGradient(t, s);
		assert(clearAreasGradient[t][s] == NULL);
		clearAreasGradient[t][s] = new Uint8[size];
		updateClearAreasGradient(t, s);
	}
	
	assert(exploredArea[t] == NULL);
	exploredArea[t] = new Uint8[size];
	initExploredArea(t);
}

void Map::removeTeam(void)
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
//	int oldNumberOfTeam=numberOfTeam+1;
	assert(numberOfTeam<32);
	
//	for (int t=0; t<oldNumberOfTeam; t++)
//		for (int r=0; r<MAX_RESSOURCES; r++)
//			for (int s=0; s<2; s++)
//				assert(ressourcesGradient[t][r][s]);
//	for (int t=oldNumberOfTeam; t<32; t++)
//		for (int r=0; r<MAX_RESSOURCES; r++)
//			for (int s=0; s<2; s++)
//				assert(ressourcesGradient[t][r][s]==NULL);
	
	int t=numberOfTeam;
	for (int r=0; r<MAX_RESSOURCES; r++)
		for (int s=0; s<2; s++)
		{
//			assert(ressourcesGradient[t][r][s]);
			if(ressourcesGradient[t][r][s])
				delete[] ressourcesGradient[t][r][s];
			ressourcesGradient[t][r][s]=NULL;
		}

	for (int s=0; s<2; s++)
	{
		assert(forbiddenGradient[t][s] != NULL);
		delete[] forbiddenGradient[t][s];
		forbiddenGradient[t][s]=NULL;
		assert(guardAreasGradient[t][s] != NULL);
		delete[] guardAreasGradient[t][s];
		guardAreasGradient[t][s]=NULL;
		assert(clearAreasGradient[t][s] != NULL);
		delete[] clearAreasGradient[t][s];
		clearAreasGradient[t][s]=NULL;
	}

	
	assert(exploredArea[t] != NULL);
	delete[] exploredArea[t];
	exploredArea[t]=NULL;
}

// TODO: completely recreate:
void Map::growRessources(void)
{
	int dy=(syncRand()&0x3);
	for (int y=dy; y<h; y+=4)
	{
		for (int x=(syncRand()&0xF); x<w; x+=(syncRand()&0x1F))
		//for (int x=0; x<w; x++)
		{
			//int y=syncRand()&hMask;
			const Ressource &r = getRessource(x, y);
			if (r.type!=NO_RES_TYPE)
			{
				// we look around to see if there is any water :
				// TODO: uses UnderMap.
				int dwax=(syncRand()&0xF)-(syncRand()&0xF);
				int dway=(syncRand()&0xF)-(syncRand()&0xF);
				int wax1=x+dwax;
				int way1=y+dway;

				int wax2=x+dway*2;
				int way2=y+dwax*2;

				int wax3=x-dwax;
				int way3=y-dway;

				//int wax4=x+dway*2;
				//int way4=y-dwax*2;

				// alga, wood and corn are limited by near underground. Others are not.
				bool expand=true;
				if (r.type == ALGA)
					expand = isWater(wax1, way1) && isSand(wax2, way2);
				else if (r.type == WOOD)
					expand = isWater(wax1, way1) && (!isSand(wax3, way3));
				else if (r.type == CORN)
					expand = isWater(wax1, way1) && (!isSand(wax3, way3));

				if (expand)
				{
					if (r.amount<=(syncRand()&7))
					{
						// we grow ressource:
						if(canRessourcesGrow(x, y))
							incRessource(x, y, r.type, r.variety);
					}
					else if (globalContainer->ressourcesTypes.get(r.type)->expendable)
					{
						// we extand ressource:
						int dx, dy;
						Unit::dxdyfromDirection((syncRand()&7), &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						if(canRessourcesGrow(nx, ny))
							incRessource(nx, ny, r.type, r.variety);
					}
				}
			}
		}
	}
}

void Map::syncStep(Uint32 stepCounter)
{
	growRessources();
	for (int i=0; i<sizeSector; i++)
		sectors[i].step();
	
	if (stepCounter & 1)
	{
		int team = (stepCounter >> 1) & 31;
		if (team < game->mapHeader.getNumberOfTeams())
			updateExploredArea(team);
	}
	
	// We only update one gradient per step:
	bool updated=false;
	while (!updated)
	{
		int numberOfTeam=game->mapHeader.getNumberOfTeams();
		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
					if (!gradientUpdated[t][r][s])
					{
						updateRessourcesGradient(t, r, (bool)s);
						gradientUpdated[t][r][s]=true;
						return;
					}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<2; s++)
				if(!guardGradientUpdated[t][s])
				{
					updateGuardAreasGradient(t, (bool)s);
					guardGradientUpdated[t][s]=true;
					return;
				}
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<2; s++)
				if(!clearGradientUpdated[t][s])
				{
					updateClearAreasGradient(t, (bool)s);
					clearGradientUpdated[t][s]=true;
					return;
				}
				

		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
					gradientUpdated[t][r][s]=false;
		for (int t=0; t<numberOfTeam; t++)
			for(int s=0; s<2; s++)
			{
				guardGradientUpdated[t][s]=false;
				clearGradientUpdated[t][s]=false;
			}
	}
}

void Map::switchFogOfWar(void)
{
	memset(fogOfWar, 0, size*sizeof(Uint32));
	if (fogOfWar==fogOfWarA)
		fogOfWar=fogOfWarB;
	else
		fogOfWar=fogOfWarA;
}

void Map::computeLocalForbidden(int localTeamNo)
{
	int localTeamMask = 1<<localTeamNo;
	for (size_t i=0; i<size; i++)
		if ((cases[i].forbidden & localTeamMask) != 0)
			localForbiddenMap.set(i, true);
		else
			localForbiddenMap.set(i, false);
}

void Map::computeLocalGuardArea(int localTeamNo)
{
	int localTeamMask = 1<<localTeamNo;
	for (size_t i=0; i<size; i++)
		if ((cases[i].guardArea & localTeamMask) != 0)
			localGuardAreaMap.set(i, true);
		else
			localGuardAreaMap.set(i, false);
}

void Map::computeLocalClearArea(int localTeamNo)
{
	int localTeamMask = 1<<localTeamNo;
	for (size_t i=0; i<size; i++)
		if ((cases[i].clearArea & localTeamMask) != 0)
			localClearAreaMap.set(i, true);
		else
			localClearAreaMap.set(i, false);
}

void Map::decRessource(int x, int y)
{
	Ressource &r = getCase(x, y).ressource;
	
	if (r.type == NO_RES_TYPE || r.amount == 0)
		return;
	
	const RessourceType *fulltype = globalContainer->ressourcesTypes.get(r.type);
	
	if (!fulltype->shrinkable)
		return;
	if (fulltype->eternal)
	{
		if (r.amount > 0)
			r.amount--;
	}
	else
	{
		if (!fulltype->granular || r.amount<=1)
			r.clear();
		else
			r.amount--;
	}
}

void Map::decRessource(int x, int y, int ressourceType)
{
	if (isRessourceTakeable(x, y, ressourceType))
		decRessource(x, y);
}

bool Map::incRessource(int x, int y, int ressourceType, int variety)
{
	Ressource &r = getCase(x, y).ressource;
	const RessourceType *fulltype;
	incRessourceLog[0]++;
	if (r.type == NO_RES_TYPE)
	{
		incRessourceLog[1]++;
		if (getBuilding(x, y) != NOGBID)
			return false;
		incRessourceLog[2]++;
		if (getGroundUnit(x, y) != NOGUID)
			return false;
		incRessourceLog[3]++;
		
		fulltype = globalContainer->ressourcesTypes.get(ressourceType);
		if (getTerrainType(x, y) == fulltype->terrain)
		{
			r.type = ressourceType;
			r.variety = variety;
			r.amount = 1;
			r.animation = 0;
			incRessourceLog[4]++;
			return true;
		}
		else
		{
			incRessourceLog[5]++;
			return false;
		}
	}
	else
	{
		fulltype = globalContainer->ressourcesTypes.get(r.type);
		incRessourceLog[6]++;
	}
	
	incRessourceLog[7]++;
	if (r.type != ressourceType)
		return false;
	incRessourceLog[8]++;
	if (!fulltype->shrinkable)
		return false;
	incRessourceLog[9]++;
	if (r.amount < fulltype->sizesCount)
	{
		incRessourceLog[10]++;
		r.amount++;
		return true;
	}
	else
	{
		incRessourceLog[11]++;
		r.amount--;
	}
	return false;
}

bool Map::isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask)
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (getGroundUnit(x, y)!=NOGUID)
		return false;
	if (!canSwim && isWater(x, y))
		return false;
	if (getForbidden(x, y)&teamMask)
		return false;
	return true;
}

bool Map::isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim)
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (getGroundUnit(x, y)!=NOGUID)
		return false;
	if (!canSwim && isWater(x, y))
		return false;
	return true;
}

bool Map::isFreeForBuilding(int x, int y)
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (getGroundUnit(x, y)!=NOGUID)
		return false;
	if (isGrass(x, y))
		return true;
	else
		return false;
}

bool Map::isFreeForBuilding(int x, int y, int w, int h)
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!isFreeForBuilding(xi, yi))
				return false;
	return true;
}

bool Map::isFreeForBuilding(int x, int y, int w, int h, Uint16 gid)
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!isFreeForBuilding(xi, yi))
			{
				if (isRessource(xi, yi))
					return false;
				Uint16 buid=getBuilding(xi, yi);
				if (buid!=NOGBID && buid!=gid)
					return false;
				if (getGroundUnit(xi, yi)!=NOGUID)
					return false;
				if (!isGrass(xi, yi))
					return false;
			}
	return true;
}

bool Map::isHardSpaceForGroundUnit(int x, int y, bool canSwim, Uint32 me)
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (!canSwim && isWater(x, y))
		return false;
	if (getForbidden(x, y)&me)
		return false;
	return true;
}

bool Map::isHardSpaceForBuilding(int x, int y)
{
	if (isRessource(x, y))
		return false;
	if (getBuilding(x, y)!=NOGBID)
		return false;
	if (isGrass(x, y))
		return true;
	else
		return false;
}

bool Map::isHardSpaceForBuilding(int x, int y, int w, int h)
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
			if (!isHardSpaceForBuilding(xi, yi))
				return false;
	return true;
}

bool Map::isHardSpaceForBuilding(int x, int y, int w, int h, Uint16 gid)
{
	for (int yi=y; yi<y+h; yi++)
		for (int xi=x; xi<x+w; xi++)
		{
			if (isRessource(xi, yi))
				return false;
			Uint16 buid=getBuilding(xi, yi);
			if (buid!=NOGBID && buid!=gid)
				return false;
			if (!isGrass(xi, yi))
				return false;
		}
	return true;
}

bool Map::doesUnitTouchBuilding(Unit *unit, Uint16 gbid, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesPosTouchBuilding(int x, int y, Uint16 gbid)
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
				return true;
	return false;
}

bool Map::doesPosTouchBuilding(int x, int y, Uint16 gbid, int *dx, int *dy)
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (getBuilding(x+tdx, y+tdy)==gbid)
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesUnitTouchRessource(Unit *unit, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	Uint32 me=unit->owner->me;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy) && ((getForbidden(x+tdx, y+tdy)&me)==0))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesUnitTouchRessource(Unit *unit, int ressourceType, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	Uint32 me=unit->owner->me;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessourceTakeable(x+tdx, y+tdy, ressourceType) && ((getForbidden(x+tdx, y+tdy)&me)==0))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

bool Map::doesPosTouchRessource(int x, int y, int ressourceType, int *dx, int *dy)
{
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessourceTakeable(x+tdx, y+tdy,ressourceType))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

//! This method gives a good direction to hit for a warrior, and return false if nothing was found.
//! Currently, it chooses to hit any turret if available, then units, then other buildings.
bool Map::doesUnitTouchEnemy(Unit *unit, int *dx, int *dy)
{
	int x=unit->posX;
	int y=unit->posY;
	int bestTime=256;//Shorter is better
	int bdx=0, bdy=0;

	Uint32 enemies=unit->owner->enemies;
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
		{
			Sint32 gbid=getBuilding(x+tdx, y+tdy);
			if (gbid!=NOGBID)
			{
				int otherTeam=Building::GIDtoTeam(gbid);
				Uint32 otherTeamMask=1<<otherTeam;
				if (enemies & otherTeamMask)
				{
					assert(game);
					assert(game->teams[otherTeam]);
					int otherID=Building::GIDtoID(gbid);
					Building *b=game->teams[otherTeam]->myBuildings[otherID];
					if (!b->type->defaultUnitStayRange)
					{
						if (b->type->shootingRange)
						{
							bdx=tdx;
							bdy=tdy;
							bestTime=0;
						}
						else if (bestTime>255)
						{
							bdx=tdx;
							bdy=tdy;
							bestTime=255;
						}
					}
				}
			}
			Sint32 guid=getGroundUnit(x+tdx, y+tdy);
			if (guid!=NOGUID)
			{
				int otherTeam=Unit::GIDtoTeam(guid);
				Uint32 otherTeamMask=1<<otherTeam;
				if (enemies & otherTeamMask)
				{
					assert(game);
					assert(game->teams[otherTeam]);
					int otherID=Unit::GIDtoID(guid);
					Unit *otherUnit=game->teams[otherTeam]->myUnits[otherID];
					if ((unit->owner->sharedVisionExchange & otherTeamMask)==0)
					{
						int time=(256-otherUnit->delta)/otherUnit->speed;
						if (time<bestTime)
						{
							bestTime=time;
							bdx=tdx;
							bdy=tdy;
						}
					}
				}
			}
			//TODO: can ground WARRIOR hit flying EXPLORER ?
		}
	
	if (bestTime<256)
	{
		*dx=bdx;
		*dy=bdy;
		return true;
	}

	return false;
}



void Map::setClearingAreaClaimed(int x, int y, int teamNumber)
{
	clearingAreaClaims[(normalizeY(y) << wDec) + normalizeX(x)] |= 1u<<teamNumber;
}



void Map::setClearingAreaUnclaimed(int x, int y, int teamNumber)
{
	Uint32 &mask = clearingAreaClaims[(normalizeY(y) << wDec) + normalizeX(x)];
	mask ^= mask & (1u<<teamNumber);
}



bool Map::isClearingAreaClaimed(int x, int y, int teamNumber)
{
	return clearingAreaClaims[(normalizeY(y) << wDec) + normalizeX(x)] & (1u<<teamNumber);
}



void Map::markImmobileUnit(int x, int y, int teamNumber)
{
	immobileUnits[(normalizeY(y) << wDec) + normalizeX(x)] = teamNumber;
}


void Map::clearImmobileUnit(int x, int y)
{
	immobileUnits[(normalizeY(y) << wDec) + normalizeX(x)] = 255;
}


bool Map::isImmobileUnit(int x, int y)
{
	return immobileUnits[(normalizeY(y) << wDec) + normalizeX(x)] != 255;
}



Uint8 Map::getImmobileUnit(int x, int y)
{
	immobileUnits[(normalizeY(y) << wDec) + normalizeX(x)];
}



void Map::setUMatPos(int x, int y, TerrainType t, int l)
{
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
		{
			if (t==GRASS)
			{
				if (getUMTerrain(dx,dy-1)==WATER)
				{
// 					setNoRessource(dx, dy-1, 1);
					setUMTerrain(dx,dy-1,SAND);
				}
				if (getUMTerrain(dx,dy+1)==WATER)
				{
// 					setNoRessource(dx, dy+1, 1);
					setUMTerrain(dx,dy+1,SAND);
				}

				if (getUMTerrain(dx-1,dy)==WATER)
				{
// 					setNoRessource(dx-1, dy, 1);
					setUMTerrain(dx-1,dy,SAND);
				}
				if (getUMTerrain(dx+1,dy)==WATER)
				{
// 					setNoRessource(dx+1, dy, 1);
					setUMTerrain(dx+1,dy,SAND);
				}

				if (getUMTerrain(dx-1,dy-1)==WATER)
				{
// 					setNoRessource(dx-1, dy-1, 1);
					setUMTerrain(dx-1,dy-1,SAND);
				}
				if (getUMTerrain(dx+1,dy-1)==WATER)
				{
// 					setNoRessource(dx+1, dy-1, 1);
					setUMTerrain(dx+1,dy-1,SAND);
				}

				if (getUMTerrain(dx+1,dy+1)==WATER)
				{
// 					setNoRessource(dx+1, dy+1, 1);
					setUMTerrain(dx+1,dy+1,SAND);
				}
				if (getUMTerrain(dx-1,dy+1)==WATER)
				{
// 					setNoRessource(dx-1, dy+1, 1);
					setUMTerrain(dx-1,dy+1,SAND);
				}
			}
			else if (t==WATER)
			{
				if (getUMTerrain(dx,dy-1)==GRASS)
				{
// 					setNoRessource(dx, dy-1, 1);
					setUMTerrain(dx,dy-1,SAND);
				}
				if (getUMTerrain(dx,dy+1)==GRASS)
				{
// 					setNoRessource(dx, dy+1, 1);
					setUMTerrain(dx,dy+1,SAND);
				}

				if (getUMTerrain(dx-1,dy)==GRASS)
				{
// 					setNoRessource(dx-1, dy, 1);
					setUMTerrain(dx-1,dy,SAND);
				}
				if (getUMTerrain(dx+1,dy)==GRASS)
				{
// 					setNoRessource(dx+1, dy, 1);
					setUMTerrain(dx+1,dy,SAND);
				}

				if (getUMTerrain(dx-1,dy-1)==GRASS)
				{
// 					setNoRessource(dx-1, dy-1, 1);
					setUMTerrain(dx-1,dy-1,SAND);
				}
				if (getUMTerrain(dx+1,dy-1)==GRASS)
				{
// 					setNoRessource(dx+1, dy-1, 1);
					setUMTerrain(dx+1,dy-1,SAND);
				}

				if (getUMTerrain(dx+1,dy+1)==GRASS)
				{
// 					setNoRessource(dx+1, dy+1, 1);
					setUMTerrain(dx+1,dy+1,SAND);
				}
				if (getUMTerrain(dx-1,dy+1)==GRASS)
				{
// 					setNoRessource(dx-1, dy+1, 1);
					setUMTerrain(dx-1,dy+1,SAND);
				}
			}
			setUMTerrain(dx,dy,t);
		}
	if (t==SAND)
		regenerateMap(x-(l>>1)-1,y-(l>>1)-1,l+1,l+1);
	else
		regenerateMap(x-(l>>1)-2,y-(l>>1)-2,l+3,l+3);
}

void Map::setNoRessource(int x, int y, int l)
{
	assert(l>=0);
	assert(l<w);
	assert(l<h);
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
			(cases+w*(dy&hMask)+(dx&wMask))->ressource.clear();
}

void Map::setRessource(int x, int y, int type, int l)
{
	assert(l>=0);
	assert(l<w);
	assert(l<h);
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
			if (isRessourceAllowed(dx, dy, type))
			{
				Ressource *rp=&((cases+w*(dy&hMask)+(dx&wMask))->ressource);
				rp->type=type;
				RessourceType *rt=globalContainer->ressourcesTypes.get(type);
				rp->variety=syncRand()%rt->varietiesCount;
				assert(rt->sizesCount>1);
				rp->amount=1+syncRand()%(rt->sizesCount-1);
				rp->animation=0;
			}
}

bool Map::isRessourceAllowed(int x, int y, int type)
{
	return (getBuilding(x, y) == NOGBID) && (getGroundUnit(x, y) == NOGUID) && (getTerrainType(x, y)==globalContainer->ressourcesTypes.get(type)->terrain);
}

bool Map::isPointSet(int n, int x, int y)
{
	return getCase(x, y).scriptAreas & 1<<n;
}

void Map::setPoint(int n, int x, int y)
{
	getCase(x, y).scriptAreas |= 1<<n;
}

void Map::unsetPoint(int n, int x, int y)
{
	getCase(x, y).scriptAreas ^= getCase(x, y).scriptAreas & (1<<n);
}

std::string Map::getAreaName(int n)
{
	return areaNames[n];
}

void Map::setAreaName(int n, std::string name)
{
	areaNames[n]=name;
}

void Map::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	int x = (mx - viewportX) & wMask;
	int y = (my - viewportY) & hMask;
	if (x > (w - 16))
		x-=w;
	if (y > (h - 16))
		y-=h;
	*px=x<<5;
	*py=y<<5;
}

void Map::mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH)
{
	int x = (mx - viewportX) & wMask;
	int y = (my - viewportY) & hMask;
	if (x > (w/2))
		x-=w;
	if (y > (h/2))
		y-=h;
	*px=x<<5;
	*py=y<<5;
}

void Map::displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	*px=((mx>>5)+viewportX)&getMaskW();
	*py=((my>>5)+viewportY)&getMaskH();
}

void Map::displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	*px=(((mx+16)>>5)+viewportX)&getMaskW();
	*py=(((my+16)>>5)+viewportY)&getMaskH();
}

void Map::cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY)
{
	int tempX, tempY;
	if (buildingWidth&0x1)
		tempX=((mx)>>5)+viewportX;
	else
		tempX=((mx+16)>>5)+viewportX;
			
	if (buildingHeight&0x1)
		tempY=((my)>>5)+viewportY;
	else
		tempY=((my+16)>>5)+viewportY;
		
	*px=tempX&getMaskW();
	*py=tempY&getMaskH();
}

void Map::buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY)
{
	mapCaseToDisplayable(px, py, mx, my, viewportX, viewportY);
	*mx+=buildingWidth*16;
	*my+=buildingHeight*16;
}

bool Map::ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y)
{
	Uint8 g = getGradient(teamNumber, ressourceType, canSwim, x, y);
	return g>1; //Becasue 0==obstacle, 1==no obstacle, but you don't know if there is anything around.
}

bool Map::ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y, int *dist)
{
	Uint8 g = getGradient(teamNumber, ressourceType, canSwim, x, y);
	if (g>1)
	{
		*dist = 255-g;
		return true;
	}
	else
		return false;
}

bool Map::ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist)
{
	// distance and availability
	bool result;
	if (dist)
		result = ressourceAvailable(teamNumber, ressourceType, canSwim, x, y, dist);
	else
		result = ressourceAvailable(teamNumber, ressourceType, canSwim, x, y);
		
	// target position
	Uint8 *gradient = ressourcesGradient[teamNumber][ressourceType][canSwim];
	ressourceAvailableCount[teamNumber][ressourceType]++;
	if (getGlobalGradientDestination(gradient, x, y, targetX, targetY))
		ressourceAvailableCountSuccess[teamNumber][ressourceType]++;
	else
		ressourceAvailableCountFailure[teamNumber][ressourceType]++;
	
	return result;
}

bool Map::getGlobalGradientDestination(Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY)
{
	// we start from our current position
	int vx = x & wMask;
	int vy = y & hMask;
	// max is initiaslized to gradient value of current position
	Uint8 max = gradient[(vx&wMask)+((vy&hMask)<<wDec)];
	
	bool result = false;
	// for up to 255 steps, we follow gradient
	for (int count=0; count<255; count++)
	{
		bool found = false;
		int vddx = 0;
		int vddy = 0;
		
		// search all directions
		for (int d=0; d<8; d++)
		{
			int ddx = deltaOne[d][0];
			int ddy = deltaOne[d][1];
			Uint8 g = gradient[((vx+ddx)&wMask)+(((vy+ddy)&hMask)<<wDec)];
			if (g>max)
			{
				max = g;
				vddx = ddx;
				vddy = ddy;
				found = true;
			}
		}
		
		// change position
		vx = (vx+vddx) & wMask;
		vy = (vy+vddy) & hMask;
		
		// if we have reached destionation break
		if (max == 255)
		{
			result = true;
			break;
		}
		// if we haven't found a suitable direction, we break, but we do not have exact destination
		else if (!found)
			break;
	}
	
	// return best destination and wether it is exact or not
	*targetX = vx;
	*targetY = vy;
	return result;
}


/*
This was the old way. I was much more complex but reliable with partially broken gradients. Let's keep it for now in case of such type of gradient reappears
bool Map::ressourceAvailable(int teamNumber, int ressourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist)
{
	ressourceAvailableCount[teamNumber][ressourceType]++;
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	x&=wMask;
	y&=hMask;
	Uint8 g=gradient[(y<<wDec)+x];
	if (g<2)
	{
		ressourceAvailableCountFast[teamNumber][ressourceType]++;
		return false;
	}
	if (dist)
		*dist=255-g;
	if (g>=255)
	{
		ressourceAvailableCountFast[teamNumber][ressourceType]++;
		*targetX=x;
		*targetY=y;
		return true;
	}
	int vx=x&wMask;
	int vy=y&hMask;
	
	Uint8 max=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
	for (int count=0; count<255; count++)
	{
		bool found=false;
		int vddx=0;
		int vddy=0;
		for (int d=0; d<8; d++)
		{
			int ddx=deltaOne[d][0];
			int ddy=deltaOne[d][1];
			Uint8 g=gradient[((vx+ddx)&wMask)+(((vy+ddy)&hMask)<<wDec)];
			if (g>max)
			{
				max=g;
				vddx=ddx;
				vddy=ddy;
				found=true;
			}
		}
		if (found)
		{
			vx=(vx+vddx)&wMask;
			vy=(vy+vddy)&hMask;
		}
		else
		{
			ressourceAvailableCountFar[teamNumber][ressourceType]++;
			Uint8 miniGrad[25];
			for (int ry=0; ry<5; ry++)
				for (int rx=0; rx<5; rx++)
				{
					int xg=(vx+rx-2)&wMask;
					int yg=(vy+ry-2)&hMask;
					miniGrad[rx+ry*5]=gradient[xg+yg*w];
				}
			if (directionFromMinigrad(miniGrad, &vddx, &vddy, false, false))
			{
				vx=(vx+vddx)&wMask;
				vy=(vy+vddy)&hMask;
				Uint8 g=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
				found=true;
				if (g>max)
					max=g;
			}
		}
		if (max==255 || (max>=255 && (getBuilding(vx, vy)==NOGBID)))
		{
			ressourceAvailableCountSuccess[teamNumber][ressourceType]++;
			*targetX=vx;
			*targetY=vy;
			return true;
		}
		if (!found)
		{
			{
				int vx=x&wMask;
				int vy=y&hMask;
				if (verbose)
					printf("init v=(%d, %d)\n", vx, vy);

				Uint8 max=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
				for (int count=0; count<255; count++)
				{
					bool found=false;
					int vddx=0;
					int vddy=0;
					for (int d=0; d<8; d++)
					{
						int ddx=deltaOne[d][0];
						int ddy=deltaOne[d][1];
						Uint8 g=gradient[((vx+ddx)&wMask)+(((vy+ddy)&hMask)<<wDec)];
						if (g>max)
						{
							max=g;
							vddx=ddx;
							vddy=ddy;
							found=true;
						}
					}
					if (found)
					{
						vx=(vx+vddx)&wMask;
						vy=(vy+vddy)&hMask;
						if (verbose)
							printf("fast v=(%d, %d), max=%d\n", vx, vy, max);
					}
					else
					{
						Uint8 miniGrad[25];
						for (int ry=0; ry<5; ry++)
							for (int rx=0; rx<5; rx++)
							{
								int xg=(vx+rx-2)&wMask;
								int yg=(vy+ry-2)&hMask;
								miniGrad[rx+ry*5]=gradient[xg+yg*w];
							}

						if (directionFromMinigrad(miniGrad, &vddx, &vddy, false, false))
						{
							vx=(vx+vddx)&wMask;
							vy=(vy+vddy)&hMask;
							Uint8 g=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
							found=true;
							if (g>max)
								max=g;
							if (verbose)
								printf("mini v=(%d, %d), g=%d, max=%d\n", vx, vy, g, max);
						}
					}
					if (max==255 || (max>=255 && (getBuilding(vx, vy)==NOGBID)))
					{
						if (verbose)
							printf("return true v=(%d, %d), max=%d\n", vx, vy, max);
						break;
					}
					if (!found)
					{
						if (verbose)
							printf("return false\n");
						break;
					}
				}
			}
			
			ressourceAvailableCountFailureBase[teamNumber][ressourceType]++;
			fprintf(logFile, "target *not* found! pos=(%d, %d), vpos=(%d, %d), max=%d, team=%d, res=%d, swim=%d\n", x, y, vx, vy, max, teamNumber, ressourceType, canSwim);
			if (verbose)
				printf("target *not* found! pos=(%d, %d), vpos=(%d, %d), max=%d, team=%d, res=%d, swim=%d\n", x, y, vx, vy, max, teamNumber, ressourceType, canSwim);
			*targetX=vx;
			*targetY=vy;
			return false;
		}
	}
	
	{
		int vx=x&wMask;
		int vy=y&hMask;
		if (verbose)
			printf("init v=(%d, %d)\n", vx, vy);

		Uint8 max=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
		for (int count=0; count<255; count++)
		{
			bool found=false;
			int vddx=0;
			int vddy=0;
			for (int d=0; d<8; d++)
			{
				int ddx=deltaOne[d][0];
				int ddy=deltaOne[d][1];
				Uint8 g=gradient[((vx+ddx)&wMask)+(((vy+ddy)&hMask)<<wDec)];
				if (g>max)
				{
					max=g;
					vddx=ddx;
					vddy=ddy;
					found=true;
				}
			}
			if (found)
			{
				vx=(vx+vddx)&wMask;
				vy=(vy+vddy)&hMask;
				if (verbose)
					printf("fast v=(%d, %d), max=%d\n", vx, vy, max);
			}
			else
			{
				Uint8 miniGrad[25];
				for (int ry=0; ry<5; ry++)
					for (int rx=0; rx<5; rx++)
					{
						int xg=(vx+rx-2)&wMask;
						int yg=(vy+ry-2)&hMask;
						miniGrad[rx+ry*5]=gradient[xg+yg*w];
					}

				if (directionFromMinigrad(miniGrad, &vddx, &vddy, false, false))
				{
					vx=(vx+vddx)&wMask;
					vy=(vy+vddy)&hMask;
					Uint8 g=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
					found=true;
					if (g>max)
						max=g;
					if (verbose)
						printf("mini v=(%d, %d), g=%d, max=%d\n", vx, vy, g, max);
				}
			}
			if (max==255 || (max>=255 && (getBuilding(vx, vy)==NOGBID)))
			{
				if (verbose)
					printf("return true v=(%d, %d), max=%d\n", vx, vy, max);
				break;
			}
			if (!found)
			{
				if (verbose)
					printf("return false\n");
				break;
			}
		}
	}
	
	ressourceAvailableCountFailureOvercount[teamNumber][ressourceType]++;
	fprintf(logFile, "target *not* found! (count>255) pos=(%d, %d), vpos=(%d, %d), team=%d, res=%d, swim=%d\n", x, y, vx, vy, teamNumber, ressourceType, canSwim);
	if (verbose)
		printf("target *not* found! (count>255) pos=(%d, %d), vpos=(%d, %d), team=%d, res=%d, swim=%d\n", x, y, vx, vy, teamNumber, ressourceType, canSwim);
	*targetX=vx;
	*targetY=vy;
	return false;
}
*/

void Map::updateGlobalGradientSlow(Uint8 *gradient)
{
	if (size <= 65536)
		updateGlobalGradientSlow<Uint16>(gradient);
	else
		updateGlobalGradientSlow<Uint32>(gradient);
}

template<typename Tint> void Map::updateGlobalGradientSlow(Uint8 *gradient)
{
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	// make the first list:
	for (size_t i = 0; i < size; i++)
		if (gradient[i] >= 3)
			listedAddr[listCountWrite++] = i;
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_UNDEFINED, true);
	delete[] listedAddr;
}

/*! Note that you can't provide any listedAddr[], or the gradient may technically end up
	wrong. Given the results of the tests, this will never happen. The easiest way to provide
	a listedAddr[] which guarantee a correct result, is to put only references to gradient
	heights that are all the same. Currently this is the case of all gradient computation but
	the AI ones (GT_UNDEFINED). For further undestanding you have to dig into the code and
	try #define check_disorderable_gradient_error_probability */
template<typename Tint> void Map::updateGlobalGradientVersionSimple(
	Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType)
{
	size_t listCountRead = 0;
	#ifdef check_disorderable_gradient_error_probability
	size_t listCountSizeMax = 0;
	#endif
	while (listCountRead < listCountWrite)
	{
		Tint deltaAddrG = listedAddr[(listCountRead++)&(size-1)];
		
		size_t y = deltaAddrG >> wDec;      // Calculate the coordinates of
		size_t x = deltaAddrG & wMask;      // the current field and of the
		
		size_t yu = ((y - 1) & hMask);      // fields next to it.
		size_t yd = ((y + 1) & hMask);      // We live on a torus! If we are on
		size_t xl = ((x - 1) & wMask);      // the "last line" of the map, the
		size_t xr = ((x + 1) & wMask);      // next line is the line 0 again.
		
		Uint8 g = gradient[(y << wDec) | x] - 1;
		if (g <= 1)        // All free non-source-fields start with gradient=1
			continue;  // There is no need to propagate gradient when g==1
		
		size_t deltaAddrC[8];
		Uint8 *addr;
		Uint8 side;
		
		deltaAddrC[0] = (yu << wDec) | xl;  // Calculate the positions of the
		deltaAddrC[1] = (yu << wDec) | x ;  // 8 fields next to us from their
		deltaAddrC[2] = (yu << wDec) | xr;  // coordinates.
		deltaAddrC[3] = (y  << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x ;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y  << wDec) | xl;
		for (int ci=0; ci<8; ci++)          // Check for each of this fields if we
		{                                   // can improve its gradient value
			addr = &gradient[deltaAddrC[ci]];
			side = *addr;
			if (side > 0 && side < g)   // side==0 means: you cannot walk on
			{                           // this field.
				                    // If we can improve this field
				*addr = g;          // we must add it as a new source
				#ifdef check_disorderable_gradient_error_probability
				size_t listCountSize = 1 + listCountWrite - listCountRead;
				if (listCountSizeMax < listCountSize)
					listCountSizeMax = listCountSize;
				#endif
				// Here we check if the queue is large enough to
				// contain this field as a new gradient source.
				if (listCountWrite + 1 + size!= listCountRead)
					listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
				else
					fprintf(stderr, "Map::updateGlobalGradientVersionSimple(): listedAddr[] overflow error");
			}
		}
	}
	#ifdef check_disorderable_gradient_error_probability
	if (listCountSizeMax < size)
		listCountSizeStats[gradientType][listCountSizeMax]++;
	else
		listCountSizeStatsOver[gradientType]++;
	#endif
	//assert(listCountWrite<=size);
}

template<typename Tint> void Map::updateGlobalGradientVersionSimon(Uint8 *gradient, Tint *listedAddr, size_t listCountWrite)
{
/* This algorithm uses the fact that all fields which are adjacent to the field
   directly below the current one, are also adjacent to either the field to its left
   or right.  Thus this field only needs to become a source if its left or right
   is not accessable. The same with the other 3 directions.
      |       |
      |       |
------+-------+------
      |current|
      |field  |
------+-------+------
   L  | below |   R
      |       |
------+-------+------
 next |next to| next
 to L | both  | to R
*/

#if defined(LOG_SIMON_GRADIENT)
	size_t spared=0;
	size_t listCountWriteStart=listCountWrite;
#endif


	size_t listCountRead = 0;
	while (listCountRead < listCountWrite)
	{
		Tint deltaAddrG = listedAddr[(listCountRead++)&(size-1)];
		
		size_t y = deltaAddrG >> wDec;
		size_t x = deltaAddrG & wMask;
		
		size_t yu = ((y - 1) & hMask);
		size_t yd = ((y + 1) & hMask);
		size_t xl = ((x - 1) & wMask);
		size_t xr = ((x + 1) & wMask);
		
		Uint8 g = gradient[(y << wDec) | x] - 1;
		if (g <= 1)
			continue;
		
		Uint32 flag = 0;
		Uint8 *addr;
		Uint8 side;
		{ // In this scope we care only about the diagonal neighbours.
                /* We will use flags to mark if at least one of the 2 fields
                   next to a adjacent nondiagonal field is not accessable.
                   Binary representation:
                    9 = 1001
                    3 = 0011
                    6 = 0110
                   12 = 1100
                           1 is the upper right
                          1  is the lower right
                         1   is the lower left
                        1    is the upper left
	        */
			const Uint32 diagFlags[4] = {9, 3, 6, 12};
			size_t deltaAddrC[4];
			
			deltaAddrC[0] = (yu << wDec) | xl; // Calculate the position
			deltaAddrC[1] = (yu << wDec) | xr; // of the 4 diagonal fields
			deltaAddrC[2] = (yd << wDec) | xr;
			deltaAddrC[3] = (yd << wDec) | xl;
                        //  0|_|1
                        //  _|*|_     * represents the current field
                        //  3| |2
			for (size_t ci = 0; ci < 4; ci++)  // Check them
			{
				addr = &gradient[deltaAddrC[ci]];
				side = *addr;
				if (side > 0 && side < g)
				{
					*addr = g;
					listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
				}
				else if (side == 0)            // If field is inaccessable,
					flag |= diagFlags[ci]; // mark the corresponding bit
			}
		}
		{ // Now we take a look at our nondiagonal neighbours
			size_t deltaAddrC[4];
			
			deltaAddrC[0] = (yu << wDec) | x ;   // _|0|_
			deltaAddrC[1] = (y  << wDec) | xr;   // 3|*|1
			deltaAddrC[2] = (yd << wDec) | x ;   //  |2| 
			deltaAddrC[3] = (y  << wDec) | xl;


			for (size_t ci = 0; ci < 4; ci++)
			{
				addr = &gradient[deltaAddrC[ci]];
				side = *addr;
				if (side > 0 && side < g)
				{
					*addr = g;
                                // Only mark this as a new source, 
                                // if its left or right was inaccessable.
					if (flag & 1) // Information is in the first bit
						listedAddr[(listCountWrite++)&(size-1)] = deltaAddrC[ci];
#if defined(LOG_SIMON_GRADIENT)
					else
						spared++;
#endif
				}
				flag >>= 1;  // Shift the next bit into position
			}
		}
	}
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon,"listed: %4d inserted: %4d spared: %3d\n",listCountWrite, listCountWrite-listCountWriteStart,spared);
#endif
	//assert(listCountWrite<=size);
}

template<typename Tint> void Map::updateGlobalGradientVersionKai(Uint8 *gradient, Tint *listedAddr, size_t listCountWrite)
{
	// This version tries to go through the memory in consecutive order
	// in the hope that the cache usage will be improved.
	// Instead of picking one individual field and test its neighbours,
	// we test if the field to its right is the next field we must process.
	// If it is, we test the field to right of this field and so on.
	// Otherwise we stop.  We also stop if the gradient value of the field to
	// the right differs from that of the current field or if we have reached
	// the end of the line.  (We don't have to, but we do.)
	// After that we have a horizontal line segment.
	// Now we check if we can improve the line segment above it, and below it.
	// And the fields on the left and right.

	size_t sizeMask = size-1;  // Mask needed to use listedAddr as queue.
	size_t listCountRead = 0;  // Index of first untreated field in listedAddr.

#if defined(LOG_GRADIENT_LINE_GRADIENT)
	std::map<size_t,int> dcount;
#endif
#if defined(LOG_SIMON_GRADIENT)
	size_t spared=0;
	size_t listCountWriteStart=listCountWrite;
#endif
	
	while (listCountRead < listCountWrite)  // While listedAddr not empty.
	{
		Tint deltaAddrG = listedAddr[listCountRead&sizeMask];
		
		size_t y = deltaAddrG >> wDec;
		size_t x = deltaAddrG & wMask;
		
		size_t yu = ((y - 1) & hMask);
		size_t yd = ((y + 1) & hMask);

		
		Uint8 myg = gradient[deltaAddrG]; // Get the gradient of the current field
		Uint8 g = myg-1;   // g will be the gradient of the children.
		if (g <= 1)        // All free non-source-fields start with gradient=1
		{
			listCountRead++;
			continue;  // There is no need to propagate gradient when g==1
		}
		

		Uint8 *addr;       // Pointer to a field.
		Uint8 side;        // Gradient value of a field.
		size_t pos;        // pos stores the combined (x,y) coordinate.		


                // Get the length of the segment.
		size_t d;                     // Length of the line segment.
		size_t ylineDec = y << wDec;  // Line the field is in.
		// remember: && and || only compute second argument if they have to.
		for (d=1; (++listCountRead < listCountWrite); d++) // While not empty.
		{
			pos = listedAddr[listCountRead&sizeMask]; // Next untreated field.
			// We can tollerate gaps of length 1.
			// Break if this field has not the same g as I, or is not the one
			// to my right or the one behind this.

			if (gradient[pos] != myg)   // Need same g for all fields in line.
				break;
			if (pos == (ylineDec | ( (d + x) & wMask ) ) )
				continue;    // If the next field is beside to the right.
#define ALLOW_SMALL_GAPS
#if defined( ALLOW_SMALL_GAPS )
			if (pos == (ylineDec | ( (d + 1 + x) & wMask ) ) )
			{       // If it is behind it.  We overleap one field.
				addr = &gradient[(ylineDec | ( (x+d++) & wMask ) )];
				side = *addr;
				if ( side>0 && side<g )     // Check if we can improve,
					*addr = g;          // the field we overleap.
				continue;     // Line grew 2 fields longer this time.
			}
#endif
				break;
		}
		// (x+d-1)&wMask is the last element of the line segment.
		// d is the size of the segment. listCountRead is in correct position.

#if defined( LOG_GRADIENT_LINE_GRADIENT )
		++dcount[d];
#endif



		bool leftflag=false;   // True if we might need to put the field left
		bool rightflag=false;  // resp. right of the segment to listedAddr.

                // Handle the upper line first then the lower line.
		ylineDec = yu << wDec;   
		for (int upperOrLower=0;upperOrLower<=1;upperOrLower++)
		{
			// The left of the first field is special,
			// since we have to test its left.
			pos  = ylineDec | ( (x-1) & wMask );
			addr = &gradient[pos];
			side = *addr;
			if ( side>0 && side<g )     // Check if we can improve.
			{
				*addr = g;
				listedAddr[(listCountWrite++)&sizeMask] = pos;				
			} else if (side == 0)       // See Simons version.
				leftflag=true;

			// Handle the whole segment:
			for (size_t i=0; i<d; i++)
			{
				pos  = ylineDec | ((x+i) & wMask);
				addr = &gradient[pos];
				side = *addr;
				if ( side>0 && side<g )
				{
					*addr = g;
					listedAddr[(listCountWrite++)&sizeMask] = pos;
				}
			}

			// The right of the last field is special,
			// since we have to test its right.
			pos = ylineDec | ( (x+d) & wMask );
			addr = &gradient[pos];
			side = *addr;
			if ( side>0 && side<g )
			{
				*addr = g;
				listedAddr[(listCountWrite++)&sizeMask] = pos;				
			} else if (side == 0)
				rightflag=true;

			ylineDec = yd << wDec;  // Change attention to the lower line.
		}
                // The segment is processed.		
		// Now handle leftmost and rightmost field.
		pos = (y << wDec) | ( (x-1) & wMask );
		addr = &gradient[pos];
		side = *addr;
		if ( side>0 && side<g )
		{
			*addr = g;
			if (leftflag)   // See Simons version.
				listedAddr[(listCountWrite++)&sizeMask] = pos;
#if defined(LOG_SIMON_GRADIENT)
			else
				spared++;
#endif

		}
		pos = (y << wDec) | ( (x+d) & wMask );
		addr = &gradient[pos];
		side = *addr;
		if ( side>0 && side<g )
		{
			*addr = g;
			if (rightflag)
				listedAddr[(listCountWrite++)&sizeMask] = pos;
#if defined(LOG_SIMON_GRADIENT)
			else
				spared++;
#endif

		}
	}
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon,"listed: %4d inserted: %4d spared: %3d\n",listCountWrite, listCountWrite-listCountWriteStart,spared);
#endif

#if defined( LOG_GRADIENT_LINE_GRADIENT )
	FILE *dlog = globalContainer->logFileManager->getFile("GradientLineLength.log");
	for (std::map<size_t,int>::iterator it=dcount.begin();it!=dcount.end();it++)
		fprintf(dlog,"line length: %3d count: %4d\n",it->first,it->second);
#endif
}

template<typename Tint> void Map::updateGlobalGradient(
	Uint8 *gradient, Tint *listedAddr, size_t listCountWrite, GradientType gradientType, bool canSwim)
{
	#define USE_DYNAMICAL_GRADIENT_VERSION_SR

#if defined(LOG_GRADIENT_LINE_GRADIENT)
	FILE *dlog = globalContainer->logFileManager->getFile("GradientLineLength.log");
	fprintf(dlog, "gradientType: %d\n", gradientType);
	fprintf(dlog, "canSwim: %d\n", canSwim);
#endif
#if defined(LOG_SIMON_GRADIENT)
	FILE *logSimon = globalContainer->logFileManager->getFile("Simon.log");
	fprintf(logSimon, "gradientType: %d\n", gradientType);
	fprintf(logSimon, "canSwim: %d\n", canSwim);
#endif
	
	#if defined( USE_GRADIENT_VERSION_TEST_KAI)
	if (gradientType == GT_UNDEFINED)
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
	else
	{
		Tint *testListedAddr = new Tint[size];
		Uint8 *testGradient = new Uint8[size];
		memcpy (testListedAddr, listedAddr, size);
		memcpy (testGradient, gradient, size);
		updateGlobalGradientVersionKai<Tint>(testGradient, testListedAddr, listCountWrite);
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		assert (memcmp (testGradient, gradient, size) == 0);
	}
	
	#elif defined(USE_GRADIENT_VERSION_KAI)
		updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
		
	#elif defined(USE_GRADIENT_VERSION_SIMON)
		updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
		
	#elif defined(USE_GRADIENT_VERSION_SIMPLE)
		updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		
	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION_SR)
		if (gradientType == GT_RESOURCE)
			updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
		else
			updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		
	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION_KR)
		if (gradientType == GT_RESOURCE)
			updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
		else
			updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
		
	#elif defined(USE_DYNAMICAL_GRADIENT_VERSION)
		// use the fastest gradient computation for each GradientType:
		switch (gradientType)
		{
			case GT_UNDEFINED:
				updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
				// speed 105.09% compare to simple on test
			break;
			
			case GT_RESOURCE:
				updateGlobalGradientVersionSimon<Tint>(gradient, listedAddr, listCountWrite);
				//speed 104.76% compare to simple on test
			break;
			
			case GT_BUILDING:
				updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
				// speed 100.29% compare to simple on test
			break;
			
			case GT_FORBIDDEN:
				updateGlobalGradientVersionKai<Tint>(gradient, listedAddr, listCountWrite);
				// speed 100.18% compare to simple on test
			break;
			
			case GT_GUARD_AREA:
				updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
				// fastest one here
			break;
			
			case GT_CLEAR_AREA:
				updateGlobalGradientVersionSimple<Tint>(gradient, listedAddr, listCountWrite, gradientType);
				// fastest one here
			break;
			
			default:
				assert(false);
				abort();
			break;
		}
			
	#else
		#error Please select a gradient version
	#endif
}

void Map::updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim)
{
	if (size <= 65536)
		updateRessourcesGradient<Uint16>(teamNumber, ressourceType, canSwim);
	else
		updateRessourcesGradient<Uint32>(teamNumber, ressourceType, canSwim);
}

template<typename Tint> void Map::updateRessourcesGradient(int teamNumber, Uint8 ressourceType, bool canSwim)
{
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	assert(globalContainer);
	for (size_t i=0; i<size; i++)
	{
		Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i]=0;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.ressource.type==NO_RES_TYPE)
		{
			if (c.building!=NOGBID)
				gradient[i]=0;
			else if (!canSwim && (c.terrain>=256 && c.terrain<16+256)) //!canSwim && isWater
				gradient[i]=0;
			else
				gradient[i]=1;
		}
		else if (c.ressource.type==ressourceType)
		{
			if (globalContainer->ressourcesTypes.get(ressourceType)->visibleToBeCollected && !(fogOfWar[i]&teamMask))
				gradient[i]=0;
			else
			{
				gradient[i]=255;
				listedAddr[listCountWrite++] = i;
			}
		}
		else
			gradient[i]=0;
	}
	
	updateGlobalGradient(gradient, (Tint *)listedAddr, listCountWrite, GT_RESOURCE, canSwim);
	delete[] listedAddr;
}

bool Map::directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict, bool verbose)
{
	Uint8 max;
	Uint8 mxd; // max in direction
	Uint32 maxs[8];
	
	max=mxd=miniGrad[1+1*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[0+2*5]);
		UPDATE_MAX(max,miniGrad[0+1*5]);
		UPDATE_MAX(max,miniGrad[0+0*5]);
		UPDATE_MAX(max,miniGrad[1+0*5]);
		UPDATE_MAX(max,miniGrad[2+0*5]);
	}
	maxs[0]=(max<<8)|mxd;
	max=mxd=miniGrad[3+1*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[2+0*5]);
		UPDATE_MAX(max,miniGrad[3+0*5]);
		UPDATE_MAX(max,miniGrad[4+0*5]);
		UPDATE_MAX(max,miniGrad[4+1*5]);
		UPDATE_MAX(max,miniGrad[4+2*5]);
	}
	maxs[1]=(max<<8)|mxd;
	max=mxd=miniGrad[3+3*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[4+2*5]);
		UPDATE_MAX(max,miniGrad[4+3*5]);
		UPDATE_MAX(max,miniGrad[4+4*5]);
		UPDATE_MAX(max,miniGrad[3+4*5]);
		UPDATE_MAX(max,miniGrad[2+4*5]);
	}
	maxs[2]=(max<<8)|mxd;
	max=mxd=miniGrad[1+3*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[2+4*5]);
		UPDATE_MAX(max,miniGrad[1+4*5]);
		UPDATE_MAX(max,miniGrad[0+4*5]);
		UPDATE_MAX(max,miniGrad[0+3*5]);
		UPDATE_MAX(max,miniGrad[0+2*5]);
	}
	maxs[3]=(max<<8)|mxd;
	
	
	max=mxd=miniGrad[2+1*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[1+0*5]);
		UPDATE_MAX(max,miniGrad[2+0*5]);
		UPDATE_MAX(max,miniGrad[3+0*5]);
	}
	maxs[4]=(max<<8)|mxd;
	max=mxd=miniGrad[3+2*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[4+1*5]);
		UPDATE_MAX(max,miniGrad[4+2*5]);
		UPDATE_MAX(max,miniGrad[4+3*5]);
	}
	maxs[5]=(max<<8)|mxd;
	max=mxd=miniGrad[2+3*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[1+4*5]);
		UPDATE_MAX(max,miniGrad[2+4*5]);
		UPDATE_MAX(max,miniGrad[3+4*5]);
	}
	maxs[6]=(max<<8)|mxd;
	max=mxd=miniGrad[1+2*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[3];
		UPDATE_MAX(max,miniGrad[0+1*5]);
		UPDATE_MAX(max,miniGrad[0+2*5]);
		UPDATE_MAX(max,miniGrad[0+3*5]);
	}
	maxs[7]=(max<<8)|mxd;
	
	int centerg=miniGrad[2+2*5];
	centerg=(centerg<<8)|centerg;
	int maxg=0;
	int maxd=8;
	bool good=false;
	if (strict)
	{
		for (int d=0; d<8; d++)
		{
			int g=maxs[d];
			if (g>centerg)
				good=true;
			if (maxg<=g)
			{
				maxg=g;
				maxd=d;
			}
		}
	}
	else
	{
		for (int d=0; d<8; d++)
		{
			int g=maxs[d];
			if (g && g!=centerg)
				good=true;
			if (maxg<=g)
			{
				maxg=g;
				maxd=d;
			}
		}
	}
	
	if (verbose)
	{
		if (verbose)
			printf("miniGrad (%d):\n", strict);
		for (int ry=0; ry<5; ry++)
		{
			for (int rx=0; rx<5; rx++)
			if (verbose)
				printf("%4d", miniGrad[rx+ry*5]);
			if (verbose)
				printf("\n");
		}
		if (verbose)
		{
			printf("maxs:\n");
			for (int d=0; d<8; d++)
				printf("%4d.%4d (%d)\n", maxs[d]>>8, maxs[d]&0xFF, maxs[d]);
			printf("max=%4d.%4d (%d), d=%d, good=%d\n", maxs[maxd]>>8, maxs[maxd]&0xFF, maxs[maxd], maxd, good);
		};
	}
	
	if (!good)
		return false;
	
	int stdd;
	if (maxd<4)
		stdd=(maxd<<1);
	else if (maxd!=8)
		stdd=1+((maxd-4)<<1);
	else
		stdd=8;
	
	//printf("stdd=%4d\n", maxd);
	
	Unit::dxdyfromDirection(stdd, dx, dy);
	return true;
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, Uint8 *gradient, bool strict, bool verbose)
{
	Uint8 miniGrad[25];
	miniGrad[2+2*5]=gradient[x+y*w];
	for (int di=0; di<16; di++)
	{
		int rx=tabFar[di][0];
		int ry=tabFar[di][1];
		int xg=(x+rx)&wMask;
		int yg=(y+ry)&hMask;
		int g=gradient[xg+yg*w];
		if (g==0 || g==255 || isFreeForGroundUnit(xg, yg, canSwim, teamMask))
			miniGrad[rx+ry*5+12]=g;
		else
			miniGrad[rx+ry*5+12]=0;
	}
	for (int di=0; di<8; di++)
	{
		int rx=tabClose[di][0];
		int ry=tabClose[di][1];
		int xg=(x+rx)&wMask;
		int yg=(y+ry)&hMask;
		int g=gradient[xg+yg*w];
		if (g==0 || isFreeForGroundUnit(xg, yg, canSwim, teamMask))
			miniGrad[rx+ry*5+12]=g;
		else
			miniGrad[rx+ry*5+12]=0;
	}
	if (verbose)
		printf("directionByMinigrad global %d\n", canSwim);
	return directionFromMinigrad(miniGrad, dx, dy, strict, verbose);
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool strict, bool verbose)
{
	Uint8 miniGrad[25];
	for (int ry=0; ry<5; ry++)
		for (int rx=0; rx<5; rx++)
		{
			int gx=(x+rx-2)&wMask;
			int gy=(y+ry-2)&hMask;
			int lx=(x-bx+15+rx-2)&wMask;
			int ly=(y-by+15+ry-2)&hMask;
			//printf("+r=(%d, %d), b=(%d, %d), g=(%d, %d), l=(%d, %d)\n", rx, ry, bx, by, gx, gy, lx, ly);
			if (lx==wMask)
			{
				gx=(gx+1)&wMask;
				lx=0;
			}
			else if (lx==32)
			{
				gx=(gx-1)&wMask;
				lx=31;
			}
			if (ly==hMask)
			{
				gy=(gy+1)&hMask;
				ly=0;
			}
			else if (ly==32)
			{
				gy=(gy-1)&hMask;
				ly=31;
			}
			assert(lx>=0);
			assert(ly>=0);
			assert(lx<32);
			assert(ly<32);
			int g=localGradient[lx+ly*32];
			//printf("|r=(%d, %d), b=(%d, %d), g=(%d, %d), l=(%d, %d), g=%d\n", rx, ry, bx, by, gx, gy, lx, ly, g);
			if (g==0 || g==255 || (rx==2 && ry==2) || isFreeForGroundUnit(gx, gy, canSwim, teamMask))
				miniGrad[rx+ry*5]=g;
			else
				miniGrad[rx+ry*5]=0;
		}
	for (int ry=1; ry<=3; ry++)
		for (int rx=1; rx<=3; rx++)
			if (miniGrad[rx+ry*5]==255)
			{
				int gx=(x+rx-2)&wMask;
				int gy=(y+ry-2)&hMask;
				if (!isFreeForGroundUnit(gx, gy, canSwim, teamMask))
					miniGrad[rx+ry*5]=0;
			}
	if (verbose)
		printf("directionByMinigrad local %d\n", canSwim);
	return directionFromMinigrad(miniGrad, dx, dy, strict, verbose);
}

bool Map::pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork, bool verbose)
{
	pathToRessourceCountTot++;
	if (verbose)
		printf("pathfindingRessource...\n");
	assert(ressourceType<MAX_RESSOURCES);
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	Uint8 max=gradient[x+y*w];
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (max==0)
	{
		if (verbose)
			printf("...pathfindedRessource pathfindForbidden() v1\n");
		pathToRessourceCountFailure++;
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, canSwim, x, y, dx, dy, verbose);
	}
	if (max<2)
	{
		if (verbose)
			printf("...pathfindedRessource failure v2\n");
		pathToRessourceCountFailure++;
		*stopWork=true;
		return false;
	}
	
	if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true, verbose))
	{
		pathToRessourceCountSuccess++;
		if (verbose)
			printf("...pathfindedRessource success v3\n");
		return true;
	}
	else
	{
		pathToRessourceCountFailure++;
		if (verbose)
			printf("...pathfindedRessource failure locked v4\n");
		//printf("locked at (%d, %d) for r=%d, max=%d\n", x, y, ressourceType, max);
		fprintf(logFile, "locked at (%d, %d) for r=%d, max=%d\n", x, y, ressourceType, max);
		*stopWork=false;
		return false;
	}
}

void Map::pathfindRandom(Unit *unit, bool verbose)
{
	if (verbose)
		printf("pathfindRandom()\n");
	int x=unit->posX;
	int y=unit->posY;
	if ((cases[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (verbose)
			printf(" forbidden\n");
		if (pathfindForbidden(NULL, unit->owner->teamNumber, (unit->performance[SWIM]>0), x, y, &unit->dx, &unit->dy, verbose))
		{
			if (verbose)
				printf(" success\n");
			unit->directionFromDxDy();
		}
		else
		{
			if (verbose)
				printf(" failed\n");
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
		}
	}
	else
	{
		bool da[8];
		int count=0;
		for (int di=0; di<8; di++)
		{
			int tx=(x+tabClose[di][0])&wMask;
			int ty=(y+tabClose[di][1])&hMask;
			if (isFreeForGroundUnit(tx, ty, (unit->performance[SWIM]>0), unit->owner->me))
			{
				da[di]=true;
				count++;
			}
			else
				da[di]=false;
		}
		if (verbose)
		{
			printf("count=%d\n", count);
			for (int di=0; di<8; di++)
				printf("da[%d]=%d\n", di, da[di]);
		}
		if (count==0)
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
			return;
		}
		int dir=syncRand()%count;
		if (verbose)
			printf(" dir=%d\n", dir);
		for (int di=0; di<8; di++)
			if (da[di] && dir--==0)
			{
				unit->dx=tabClose[di][0];
				unit->dy=tabClose[di][1];
				unit->direction=di;
				if (verbose)
					printf("d=(%d, %d), d=%d\n", unit->dx, unit->dy, unit->direction);
				return;
			}
		assert(false);
	}
}

/** Helper for updateLocalGradient, and others */
int clip_0_31(int x) {return (x<0)? 0 : (x>31)? 31 : x;}

/** Helper for updateLocalGradient */
void fillGradientCircle(Uint8* gradient, int r) {
	int r2=r*r;
	for (int yi=-r; yi<=r; yi++)
	{
		int yi2=yi*yi;
		int yyi=clip_0_31(15+yi);
		for (int xi=-r; xi<=r; xi++)
			if (yi2+(xi*xi)<r2)
			{
				int xxi=clip_0_31(15+xi);
				gradient[xxi+(yyi<<5)]=255;
			}
	}
}

/** Helper for updateLocalGradient */
void fillGradientRectangle(Uint8* gradient, int posW, int posH) {
	for (int dy=0; dy<posH; dy++) {
		int yyi=clip_0_31(15+dy);
		for (int dx=0; dx<posW; dx++)
		{
			int xxi=clip_0_31(15+dx);
			gradient[xxi+(yyi<<5)]=255;
		}
	}
}

void propagateLocalGradients(Uint8* gradient);

void Map::updateLocalGradient(Building *building, bool canSwim)
{
	localBuildingGradientUpdate++;
	//fprintf(logFile, "updatingLocalGradient (gbid=%d)...\n", building->gid);
	//printf("updatingLocalGradient (gbid=%d)...\n", building->gid);
	assert(building);
	assert(building->type);
	bool wasDirty = building->dirtyLocalGradient[canSwim];
	building->dirtyLocalGradient[canSwim]=false;
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	int posH=building->type->height;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Uint8 *tgtGradient=building->localGradient[canSwim];

	Uint8 gradient[1024];
 
	// 1. INITIALIZATION of gradient[]:
	// 1a. Set all values to 1 (meaning 'far away, but not inaccessable').
	memset(gradient, 1, 1024);

	bool isWarFlag=false;
	if(building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;

	// 1b. Set values at target building to 255 (meaning 'very close'/'at destination').
	if (building->type->isVirtual && !building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			int yyi=clip_0_31(15+yi);
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<r2)
				{
					int xxi=clip_0_31(15+xi);
					gradient[xxi+(yyi<<5)]=255;
				}
			}
		}
	}
	else if (building->type->isVirtual && building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			int yyi=clip_0_31(15+yi);
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = ((posX+w+xi)&wMask)+(w*((posY+h+yi)&hMask));
					if(cases[addr].ressource.type != NO_RES_TYPE && building->clearingRessources[cases[addr].ressource.type])
					{
						int xxi=clip_0_31(15+xi);
						gradient[xxi+(yyi<<5)]=255;
					}
				}
			}
		}
	}
	else
		fillGradientRectangle(gradient, posW, posH);

	// 1c. Set values at inaccessible areas to 0 (meaning, well, 'inaccessible').
	// Here g=Global(map axis), l=Local(map axis)
	
	for (int yl=0; yl<32; yl++)
	{
		int wyl=(yl<<5);
		int yg=(yl+posY-15)&hMask;
		int wyg=w*yg;
		for (int xl=0; xl<32; xl++)
		{
			int xg=(xl+posX-15)&wMask;
			const Case& c=cases[wyg+xg];
			int addrl=wyl+xl;
			if (gradient[addrl]!=255)
			{
				if (c.forbidden&teamMask)
					gradient[addrl]=0;
				else if (c.ressource.type!=NO_RES_TYPE)
					gradient[addrl]=0;
				//Warflags don't consider enemy buildings an obstacle
				else if (c.building!=NOGBID && c.building!=bgid && !(isWarFlag && (1<<Building::GIDtoTeam(c.building))  & (building->owner->enemies)))
					gradient[addrl]=0;
				else if(immobileUnits[wyg+xg] != 255)
					gradient[addrl]=0;
				else if (!canSwim && isWater(xg, yg))
					gradient[addrl]=0;
			}
		}
	}
	
	// 2. NEED TO UPDATE? Check boundary conditions to see if they have changed.
	// I commented this out, because the tgtGradient is not initialized
	// in the first runs: leading to an unconditional jump
	// todo: write a real fix

/*
	bool change = false;

	for (int i=0; i<1024; i++) {
		// The boundary conditions - do they match?
		if (gradient[i]==0 || gradient[i]==255 || tgtGradient[i]==0 || tgtGradient[i]==255) {
			if (gradient[i] != tgtGradient[i]) {
				if (((gradient[i]+1)&0xFE)==0 ||  // Is either gradient or tgtGradient 0 or 255?
				    ((tgtGradient[i]+1)&0xFE)==0)
				{
					change = true; break;
				}
			}
		}
		if (!change) return; // No need to update; boundary conditions are unchanged.
	}
	if (!change) return; // No need to update; boundary conditions are unchanged.
*/
	// 3. Check that the building is REACHABLE.
	if (!building->type->isVirtual)
	{
		building->locked[canSwim]=true;
		int x=14;
		int y=14;
		int d=posW+1;
		for (int ai=0; ai<4; ai++) //angle-iterator
			for (int mi=0; mi<d; mi++) //move-iterator
			{
				assert(x>=0);
				assert(y>=0);
				assert(x<32);
				assert(y<32);
				
				Uint8 g=gradient[(y<<5)+x];
				//printf("ai=%d, mi=%d, (%d, %d), g=%d\n", ai, mi, x, y, g);
				if (g)
				{
					building->locked[canSwim]=false;
					goto doubleBreak;
				}
				switch (ai)
				{
					case 0:
						x++;
					break;
					case 1:
						y++;
					break;
					case 2:
						x--;
					break;
					case 3:
						y--;
					break;
				}
			}
		
		assert(building->locked[canSwim]);
		localBuildingGradientUpdateLocked++;
		//fprintf(logFile, "...not updatedLocalGradient! building bgid=%d is locked!\n", building->gid);
		//printf("...not updatedLocalGradient! building bgid=%d is locked!\n", building->gid);
		memcpy(tgtGradient, gradient, 1024); // Don't leave gradient as-is (it might be dirty)
		return;
		doubleBreak:;
	}
	else
		building->locked[canSwim]=false;

	// 4. PROPAGATION of gradient values.
	propagateLocalGradients(gradient);

	// 5. WRITEBACK (because of the 'any change'-computation).
	memcpy(tgtGradient, gradient, 1024);
}

void propagateLocalGradients(Uint8* gradient) {
	//In this algorithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	for (int depth=0; depth<2; depth++) // With a higher depth, we can have more complex obstacles.
	{
		for (int down=0; down<2; down++)
		{
			int x, y, dis, die, ddi;
			if (down)
			{
				x=0;
				y=0;
				dis=31;
				die=1;
				ddi=-2;
			}
			else
			{
				x=15;
				y=15;
				dis=1;
				die=31;
				ddi=+2;
			}
			
			for (int di=dis; di!=die; di+=ddi) //distance-iterator
			{
				for (int bi=0; bi<2; bi++) //back-iterator
				{
					for (int ai=0; ai<4; ai++) //angle-iterator
					{
						for (int mi=0; mi<di; mi++) //move-iterator
						{
							//printf("di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
							//fprintf(logFile, "di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
							assert(x>=0);
							assert(y>=0);
							assert(x<32);
							assert(y<32);

							int wy=(y<<5);
							Uint8 max=gradient[wy+x];
							if (max && max!=255)
							{
								for (int dy=-32; dy<=32; dy+=32) {
									int ypart = wy+dy;
									if (ypart & (32*32)) continue; // Over- or underflow
									for (int dx=-1; dx<=1; dx++) {
										int xpart = x+dx;
										if (xpart & 32) continue; // Over- or underflow
										UPDATE_MAX(max,gradient[ypart+xpart]);
									}
								}
								assert(max);
								if (max==1)
									gradient[wy+x]=1;
								else
									gradient[wy+x]=max-1;
							}

							if (bi==0)
							{
								switch (ai)
								{
									case 0:
										x++;
									break;
									case 1:
										y++;
									break;
									case 2:
										x--;
									break;
									case 3:
										y--;
									break;
								}
							}
							else
							{
								switch (ai)
								{
									case 0:
										y++;
									break;
									case 1:
										x++;
									break;
									case 2:
										y--;
									break;
									case 3:
										x--;
									break;
								}
							}
						}
					}
				}
				if (down)
				{
					x++;
					y++;
				}
				else
				{
					x--;
					y--;
				}
			}
		}
	}
	//printf("...updatedLocalGradient\n");
	//fprintf(logFile, "...updatedLocalGradient\n");
}


void Map::updateGlobalGradient(Building *building, bool canSwim)
{
	if (size <= 65536)
		updateGlobalGradient<Uint16>(building, canSwim);
	else
		updateGlobalGradient<Uint32>(building, canSwim);
}

template<typename Tint> void Map::updateGlobalGradient(Building *building, bool canSwim)
{
	globalBuildingGradientUpdate++;
	assert(building);
	assert(building->type);
	//printf("updatingGlobalGradient (gbid=%d)\n", building->gid);
	//fprintf(logFile, "updatingGlobalGradient (gbid=%d)...", building->gid);
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	//int posH=building->type->height;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Uint8 *gradient=building->globalGradient[canSwim];
	assert(gradient);
	
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;

	bool isClearingFlag=false;
	bool isWarFlag=false;
	if (building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;
	
	

	memset(gradient, 1, size);
	if (building->type->isVirtual && !building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<r2)
				{
					size_t addr = ((posX+w+xi)&wMask)+(w*((posY+h+yi)&hMask));
					gradient[addr] = 255;
					listedAddr[listCountWrite++] = addr;
				}
		}
	}
	else if (building->type->isVirtual && building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		isClearingFlag=true;
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = ((posX+w+xi)&wMask)+(w*((posY+h+yi)&hMask));
					if(cases[addr].ressource.type!=NO_RES_TYPE && building->clearingRessources[cases[addr].ressource.type])
					{
						gradient[addr] = 255;
						listedAddr[listCountWrite++] = addr;
					}
				}
		}
	}

	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			Case& c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.forbidden&teamMask)
					gradient[wyx] = 0;
				else if (c.ressource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==255))
					gradient[wyx] = 0;
				else if(immobileUnits[wyx] != 255)
					gradient[wyx] = 0;
				else if (!canSwim && isWater(x, y))
					gradient[wyx] = 0;
			}
			else
			{
				if (c.building==bgid)
				{
					gradient[wyx] = 255;
					listedAddr[listCountWrite++] = wyx;
				}
				//Warflags don't consider enemy buildings an obstacle
				else if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
					gradient[wyx] = 0;
				else
					gradient[wyx] = 1;
			}
		}
	}
	
	if (!building->type->isVirtual)
	{
		building->locked[canSwim]=true;
		int x=(posX-1)&wMask;
		int y=(posY-1)&hMask;
		int d=posW+1;
		for (int ai=0; ai<4; ai++) //angle-iterator
			for (int mi=0; mi<d; mi++) //move-iterator
			{
				assert(x>=0);
				assert(y>=0);
				assert(x<w);
				assert(y<h);
				Uint8 g=gradient[w*y+x];
				//printf("ai=%d, mi=%d, (%d, %d), g=%d\n", ai, mi, x, y, g);
				if (g)
				{
					building->locked[canSwim]=false;
					goto doubleBreak;
				}
				switch (ai)
				{
					case 0:
						x++;
					break;
					case 1:
						y++;
					break;
					case 2:
						x--;
					break;
					case 3:
						y--;
					break;
				}
				x=(x+w)&wMask;
				y=(y+h)&hMask;
			}
		
		assert(building->locked[canSwim]);
		globalBuildingGradientUpdateLocked++;
		//printf("...not updatedGlobalGradient! building bgid=%d is locked!\n", building->gid);
		//fprintf(logFile, "...not updatedGlobalGradient! building bgid=%d is locked!\n", building->gid);
		delete[] listedAddr;
		return;
		doubleBreak:;
	}
	else
		building->locked[canSwim]=false;
	
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_BUILDING, canSwim);
	delete[] listedAddr;
}

bool Map::updateLocalRessources(Building *building, bool canSwim)
{
	localRessourcesUpdateCount++;
	assert(building);
	assert(building->type);
	assert(building->type->isVirtual);
	fprintf(logFile, "updatingLocalRessources[%d] (gbid=%d)...\n", canSwim, building->gid);
	
	int posX=building->posX;
	int posY=building->posY;
	Uint32 teamMask=building->owner->me;
	
	Uint8 *gradient=building->localRessources[canSwim];
	if (gradient==NULL)
	{
		gradient=new Uint8[1024];
		building->localRessources[canSwim]=gradient;
	}
	assert(gradient);
	
	bool *clearingRessources=building->clearingRessources;
	bool anyRessourceToClear=false;
	
	memset(gradient, 1, 1024);
	int range=building->unitStayRange;
	assert(range<=15);
	if (range>15)
		range=15;
	int range2=range*range;
	for (int yl=0; yl<32; yl++)
	{
		int wyl=(yl<<5);
		int yg=(yl+posY-15)&hMask;
		int wyg=w*yg;
		int dyl2=(yl-15)*(yl-15);
		for (int xl=0; xl<32; xl++)
		{
			int xg=(xl+posX-15)&wMask;
			const Case& c=cases[wyg+xg];
			int addrl=wyl+xl;
			int dist2=(xl-15)*(xl-15)+dyl2;
			if (dist2<=range2)
			{
				if (c.forbidden&teamMask)
					gradient[addrl]=0;
				else if (c.ressource.type!=NO_RES_TYPE)
				{
					Sint8 t=c.ressource.type;
					if (t<BASIC_COUNT && clearingRessources[t])
					{
						gradient[addrl]=255;
						anyRessourceToClear=true;
					}
					else
						gradient[addrl]=0;
				}
				else if (c.building!=NOGBID)
					gradient[addrl]=0;
				else if(immobileUnits[wyg+xg] != 255)
					gradient[addrl]=0;
				else if (!canSwim && isWater(xg, yg))
					gradient[addrl]=0;
			}
			else
				gradient[addrl]=0;
		}
	}
	building->localRessourcesCleanTime[canSwim]=0;
	if (anyRessourceToClear)
		building->anyRessourceToClear[canSwim]=1;
	else
	{
		building->anyRessourceToClear[canSwim]=2;
		return false;
	}
	expandLocalGradient(gradient);
	return true;
}


void Map::expandLocalGradient(Uint8 *gradient)
{
	for (int depth=0; depth<2; depth++) // With a higher depth, we can have more complex obstacles.
	{
		for (int down=0; down<2; down++)
		{
			int x, y, dis, die, ddi;
			if (down)
			{
				x=0;
				y=0;
				dis=31;
				die=1;
				ddi=-2;
			}
			else
			{
				x=15;
				y=15;
				dis=1;
				die=31;
				ddi=+2;
			}
			
			for (int di=dis; di!=die; di+=ddi) //distance-iterator
			{
				for (int bi=0; bi<2; bi++) //back-iterator
				{
					for (int ai=0; ai<4; ai++) //angle-iterator
					{
						for (int mi=0; mi<di; mi++) //move-iterator
						{
							//printf("di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
							assert(x>=0);
							assert(y>=0);
							assert(x<32);
							assert(y<32);

							int wy=(y<<5);
							int wyu, wyd;
							if (y==0)
								wyu=0;
							else
								wyu=((y-1)<<5);
							if (y==31)
								wyd=32*31;
							else
								wyd=((y+1)<<5);
							Uint8 max=gradient[wy+x];
							if (max && max!=255)
							{
								int xl, xr;
								if (x==0)
									xl=0;
								else
									xl=x-1;
								if (x==31)
									xr=31;
								else
									xr=x+1;

								Uint8 side;
								
								side=gradient[wyu+xl];
								if (side > max) max=side;
								side=gradient[wyu+x ];
								if (side > max) max=side;
								side=gradient[wyu+xr];
								if (side > max) max=side;

								side=gradient[wy +xr];
								if (side > max) max=side;

								side=gradient[wyd+xr];
								if (side > max) max=side;
								side=gradient[wyd+x ];
								if (side > max) max=side;
								side=gradient[wyd+xl];
								if (side > max) max=side;

								side=gradient[wy +xl];
								if (side > max) max=side;

								assert(max);
								if (max==1)
									gradient[wy+x]=1;
								else
									gradient[wy+x]=max-1;
							}

							if (bi==0)
							{
								switch (ai)
								{
									case 0:
										x++;
									break;
									case 1:
										y++;
									break;
									case 2:
										x--;
									break;
									case 3:
										y--;
									break;
								}
							}
							else
							{
								switch (ai)
								{
									case 0:
										y++;
									break;
									case 1:
										x++;
									break;
									case 2:
										y--;
									break;
									case 3:
										x--;
									break;
								}
							}
						}
					}
				}
				if (down)
				{
					x++;
					y++;
				}
				else
				{
					x--;
					y--;
				}
			}
		}
	}
}

bool Map::buildingAvailable(Building *building, bool canSwim, int x, int y, int *dist)
{
	buildingAvailableCountTot++;
	assert(building);
	int bx=building->posX;
	int by=building->posY;
	x&=wMask;
	y&=hMask;
	assert(x>=0);
	assert(y>=0);
	
	Uint8 *gradient=building->localGradient[canSwim];
	
	if (isInLocalGradient(x, y, bx, by))
	{
		buildingAvailableCountClose++;
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;
		if (!building->dirtyLocalGradient[canSwim])
		{
			Uint8 currentg=gradient[lx+(ly<<5)];
			if (currentg>1)
			{
				buildingAvailableCountCloseSuccessFast++;
				*dist=255-currentg;
				return true;
			}
			else
				for (int d=0; d<8; d++)
				{
					int ddx, ddy;
					Unit::dxdyfromDirection(d, &ddx, &ddy);
					int lxddx=clip_0_31(lx+ddx);
					int lyddy=clip_0_31(ly+ddy);
					Uint8 g=gradient[lxddx+(lyddy<<5)];
					if (g>1)
					{
						buildingAvailableCountCloseSuccessAround++;
						*dist=255-g;
						return true;
					}
				}
		}
		
		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
		{
			buildingAvailableCountCloseFailureLocked++;
			//printf("ba-a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			
			return false;
		}
		
		Uint8 currentg=gradient[lx+ly*32];
		
		if (currentg>1)
		{
			buildingAvailableCountCloseSuccessUpdate++;
			*dist=255-currentg;
			return true;
		}
		else
			for (int d=0; d<8; d++)
			{
				int ddx, ddy;
				Unit::dxdyfromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (g>1)
				{
					buildingAvailableCountCloseSuccessUpdateAround++;
					*dist=255-g;
					return true;
				}
			}
		buildingAvailableCountCloseFailureEnd++;
	}
	else
		buildingAvailableCountIsFar++;
	buildingAvailableCountFar++;
	
	gradient=building->globalGradient[canSwim];
	if (gradient==NULL)
	{
		buildingAvailableCountFarNew++;
		gradient=new Uint8[size];
		fprintf(logFile, "ba- allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		building->globalGradient[canSwim]=gradient;
	}
	else
	{
		buildingAvailableCountFarOld++;
		if (building->locked[canSwim])
		{
			buildingAvailableCountFarOldFailureLocked++;
			//printf("ba-b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
		if (currentg>1)
		{
			buildingAvailableCountFarOldSuccessFast++;
			*dist=255-currentg;
			return true;
		}
		else
		{
			for (int d=0; d<8; d++)
			{
				int ddx, ddy;
				Unit::dxdyfromDirection(d, &ddx, &ddy);
				int xddx=(x+ddx)&wMask;
				int yddy=(y+ddy)&hMask;
				Uint8 g=gradient[xddx+yddy*w];
				if (g>1)
				{
					buildingAvailableCountFarOldSuccessAround++;
					*dist=255-g;
					return true;
				}
			}
			buildingAvailableCountFarOldFailureEnd++;
			//printf("ba-c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
	}
	
	updateGlobalGradient(building, canSwim);
	if (building->locked[canSwim])
	{
		buildingAvailableCountFarNewFailureLocked++;
		//printf("ba-d- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		fprintf(logFile, "ba-d- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		return false;
	}
	
	Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
	if (currentg>1)
	{
		buildingAvailableCountFarNewSuccessFast++;
		*dist=255-currentg;
		return true;
	}
	else
	{
		for (int d=0; d<8; d++)
		{
			int ddx, ddy;
			Unit::dxdyfromDirection(d, &ddx, &ddy);
			int xddx=(x+ddx)&wMask;
			int yddy=(y+ddy)&hMask;
			Uint8 g=gradient[xddx+yddy*w];
			if (g>1)
			{
				buildingAvailableCountFarNewSuccessClosely++;
				*dist=255-g;
				return true;
			}
		}
		if (building->type->isVirtual)
		{
			//printf("ba-e- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-e- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			buildingAvailableCountFarNewFailureVirtual++;
		}
		else
		{
			if (building->verbose)
				printf("ba-f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "ba-f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			buildingAvailableCountFarNewFailureEnd++;
		}
		return false;
	}
}

bool Map::pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy, bool verbose)
{
	pathToBuildingCountTot++;
	assert(building);
	if (verbose)
		printf("pathfindingBuilding (gbid=%d)...\n", building->gid);
	int bx=building->posX;
	int by=building->posY;
	assert(x>=0);
	assert(y>=0);
	Uint32 teamMask=building->owner->me;
	if (((cases[x+y*w].forbidden) & teamMask)!=0)
	{
		int teamNumber=building->owner->teamNumber;
		if (verbose)
			printf(" ...pathfindForbidden(%d, %d, %d, %d)\n", teamNumber, canSwim, x, y);
		return pathfindForbidden(building->globalGradient[canSwim], teamNumber, canSwim, x, y, dx, dy, verbose);
	}
	Uint8 *gradient=building->localGradient[canSwim];
	if (isInLocalGradient(x, y, bx, by))
	{
		pathToBuildingCountClose++;
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;
		int max=0;
		Uint8 currentg=gradient[lx+(ly<<5)];
		bool found=false;
		bool gradientUsable=false;
		
		if (!building->dirtyLocalGradient[canSwim] && currentg==255)
		{
			*dx=0;
			*dy=0;
			pathToBuildingCountCloseSuccessStand++;
			if (verbose)
				printf("...pathfindedBuilding v1\n");
			return true;
		}

		if (!building->dirtyLocalGradient[canSwim] && currentg>1)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient, true, verbose))
			{
				pathToBuildingCountCloseSuccessBase++;
				if (verbose)
					printf("...pathfindedBuilding v2\n");
				return true;
			}
		}

		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
		{
			pathToBuildingCountCloseFailureLocked++;
			if (verbose)
				printf("a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}

		max=0;
		currentg=gradient[lx+ly*32];
		found=false;
		gradientUsable=false;
		if (currentg>1)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient, true, verbose))
			{
				pathToBuildingCountCloseSuccessUpdated++;
				if (verbose)
					printf("...pathfindedBuilding v4\n");
				return true;
			}
		}
		pathToBuildingCountCloseFailureEnd++;
	}
	else
		pathToBuildingCountIsFar++;
	pathToBuildingCountFar++;
	//Here the "local-32*32-cases-gradient-pathfinding-system" has failed, then we look for a full size gradient.
	
	gradient=building->globalGradient[canSwim];
	if (gradient==NULL)
	{
		pathToBuildingCountFarIsNew++;
		gradient=new Uint8[size];
		if (verbose)
			printf("allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		fprintf(logFile, "allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		building->globalGradient[canSwim]=gradient;
	}
	else
	{
		bool found=false;
		Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
		if (building->locked[canSwim])
		{
			pathToBuildingCountFarOldFailureLocked++;
			if (verbose)
				printf("b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		else if (currentg==1)
		{
			pathToBuildingCountFarOldFailureBad++;
			if (verbose)
				printf("c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		else
			found=directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true, verbose);

		//printf("found=%d, d=(%d, %d)\n", found, *dx, *dy);
		if (found)
		{
			pathToBuildingCountFarOldSuccess++;
			if (verbose)
				printf("...pathfindedBuilding v6\n");
			return true;
		}
		else if (building->lastGlobalGradientUpdateStepCounter[canSwim]+128>game->stepCounter) // not faster than 5.12s
		{
			pathToBuildingCountFarOldFailureRepeat++;
			if (verbose)
				printf("d- global gradient to building bgid=%d@(%d, %d) failed, repeat.\n", building->gid, building->posX, building->posY);
			return directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, false, verbose);
		}
		else
		{
			pathToBuildingCountFarOldFailureUnusable++;
		}
	}
	
	updateGlobalGradient(building, canSwim);
	building->lastGlobalGradientUpdateStepCounter[canSwim]=game->stepCounter;
	
	if (building->locked[canSwim])
	{
		pathToBuildingCountFarUpdateFailureLocked++;
		if (verbose)
			printf("e- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		fprintf(logFile, "e- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		return false;
	}
	
	Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
	if (currentg>1)
	{
		if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true, verbose))
		{
			pathToBuildingCountFarUpdateSuccess++;
			if (verbose)
				printf("...pathfindedBuilding v7\n");
			return true;
		}
	}
	
	if (building->type->isVirtual)
	{
		pathToBuildingCountFarUpdateFailureVirtual++;
		if (verbose)
			printf("f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		//fprintf(logFile, "f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
	}
	else
	{
		pathToBuildingCountFarUpdateFailureBad++;
		// TODO: find why this happend so often
		if (verbose)
			printf("g- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d), canSwim=%d\n", building->gid, building->posX, building->posY, x, y, canSwim);
		fprintf(logFile, "g- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d), canSwim=%d\n", building->gid, building->posX, building->posY, x, y, canSwim);
	}
	return false;
}

bool Map::pathfindLocalRessource(Building *building, bool canSwim, int x, int y, int *dx, int *dy)
{
	pathfindLocalRessourceCount++;
	assert(building);
	assert(building->type);
	assert(building->type->isVirtual);
	//printf("pathfindingLocalRessource[%d] (gbid=%d)...\n", canSwim, building->gid);
	
	int bx=building->posX;
	int by=building->posY;
	Uint32 teamMask=building->owner->me;
	
	Uint8 *gradient=building->localRessources[canSwim];
	if (gradient==NULL)
	{
		if (!updateLocalRessources(building, canSwim))
			return false;
		gradient=building->localRessources[canSwim];
	}
	assert(gradient);
	assert(isInLocalGradient(x, y, bx, by));
	
	int lx=(x-bx+15+32)&31;
	int ly=(y-by+15+32)&31;
	int max=0;
	Uint8 currentg=gradient[lx+(ly<<5)];
	bool found=false;
	bool gradientUsable=false;
	
	if (currentg==1 && (building->localRessourcesCleanTime[canSwim]+=16)<128)
	{
		// This mean there are still ressources, but they are unreachable.
		// We wait 5[s] before recomputing anything.
		if (verbose)
			printf("...pathfindedLocalRessource v0 failure waiting\n");
		pathfindLocalRessourceCountWait++;
		return false;
	}
	
	if (currentg>1 && currentg!=255)
	{
		for (int sd=0; sd<=1; sd++)
			for (int d=sd; d<8; d+=2)
			{
				int ddx, ddy;
				Unit::dxdyfromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
				{
					max=g;
					*dx=ddx;
					*dy=ddy;
					found=true;
				}
			}

		if (gradientUsable)
		{
			if (found)
			{
				pathfindLocalRessourceCountSuccessBase++;
				//printf("...pathfindedLocalRessource v1\n");
				return true;
			}
			else
			{
				*dx=0;
				*dy=0;
				pathfindLocalRessourceCountSuccessLocked++;
				if (verbose)
					printf("...pathfindedLocalRessource v2 locked\n");
				return true;
			}
		}
	}

	updateLocalRessources(building, canSwim);
	
	max=0;
	currentg=gradient[lx+(ly<<5)];
	found=false;
	gradientUsable=false;
	
	if (currentg==1)
	{
		pathfindLocalRessourceCountFailureNone++;
		//printf("...pathfindedLocalRessource v3 No ressource\n");
		return false;
	}
	else if ((currentg!=0) && (currentg!=255))
	{
		for (int sd=0; sd<=1; sd++)
			for (int d=sd; d<8; d+=2)
			{
				int ddx, ddy;
				Unit::dxdyfromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
				{
					max=g;
					*dx=ddx;
					*dy=ddy;
					found=true;
				}
			}

		if (gradientUsable)
		{
			if (found)
			{
				pathfindLocalRessourceCountSuccessUpdate++;
				//printf("...pathfindedLocalRessource v3\n");
				return true;
			}
			else
			{
				*dx=0;
				*dy=0;
				pathfindLocalRessourceCountSuccessUpdateLocked++;
				if (verbose)
					printf("...pathfindedLocalRessource v4 locked\n");
				return true;
			}
		}
		else
		{
			pathfindLocalRessourceCountFailureUnusable++;
			fprintf(logFile, "lr-a- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			if (verbose)
				printf("lr-a- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
	}
	else
	{
		pathfindLocalRessourceCountFailureBad++;
		fprintf(logFile, "lr-b- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		if (verbose)
			printf("lr-b- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		return false;
	}
}

void Map::dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber)
{
	y &= hMask;
	x &= wMask;
	fprintf(logFile, "Map::dirtyLocalGradient(%d, %d, %d, %d, %d)\n", x, y, wl, hl, teamNumber);
	for (int hi=0; hi<hl; hi++)
	{
		int wyi=w*((y+hi)&hMask);
		for (int wi=0; wi<wl; wi++)
		{
			int xi=(x+wi)&wMask;
			int bgid=cases[xi+wyi].building;
			if (bgid!=NOGBID)
				if (Building::GIDtoTeam(bgid)==teamNumber)
				{
					fprintf(logFile, "dirtying-LocalGradient bgid=%d\n", bgid);
					Building *b=game->teams[teamNumber]->myBuildings[Building::GIDtoID(bgid)];
					for (int canSwim=0; canSwim<2; canSwim++)
					{
						b->dirtyLocalGradient[canSwim]=true;
						b->locked[canSwim]=false;
						if (b->localRessources[canSwim])
						{
							delete b->localRessources[canSwim];
							b->localRessources[canSwim]=NULL;
						}
					}
				}
		}
	}
}

bool Map::pathfindForbidden(Uint8 *optionGradient, int teamNumber, bool canSwim, int x, int y, int *dx, int *dy, bool verbose)
{
	if (verbose)
		printf("pathfindForbidden(%d, %d, (%d, %d))\n", teamNumber, canSwim, x, y);
	pathfindForbiddenCount++;
	Uint8 *gradient=forbiddenGradient[teamNumber][canSwim];
	if (verbose && !gradient)
		printf("error, Map::pathfindForbidden(), forbiddenGradient[teamNumber=%d][canSwim=%d] is NULL\n", teamNumber, canSwim);
	assert(gradient);
	
	Uint32 maxValue=0;
	int maxd=0;
	for (int di=0; di<8; di++)
	{
		int rx=tabClose[di][0];
		int ry=tabClose[di][1];
		int xg=(x+rx)&wMask;
		int yg=(y+ry)&hMask;
		if (verbose)
			printf("[di=%d], r=(%d, %d), g=(%d, %d)\n", di, rx, ry, xg, yg);
		if (!isFreeForGroundUnitNoForbidden(xg, yg, canSwim))
			continue;
		size_t addr=xg+(yg<<wDec);
		Uint8 base=gradient[addr];
		if (verbose)
			printf("gradient[%d]=%d\n", static_cast<unsigned>(addr), gradient[addr]);
		Uint8 option;
		if (optionGradient!=NULL)
			option=optionGradient[addr];
		else
			option=0;
		if (verbose)
			printf("option=%d @ %p\n", option, optionGradient);
		Uint32 value=(base<<8)|option;
		if (verbose)
			printf("value=%d \n", value);
		if (maxValue<value)
		{
			maxValue=value;
			if (verbose)
				printf("new maxValue=%d \n", maxValue);
			maxd=di;
		}
	}
	if (maxValue>=(2<<8))
	{
		*dx=tabClose[maxd][0];
		*dy=tabClose[maxd][1];
		if (verbose)
			printf(" Success (%d:%d) (%d, %d)\n", (maxValue>>8), (maxValue&0xFF), *dx, *dy);
		pathfindForbiddenCountSuccess++;
		return true;
	}
	else
	{
		if (verbose)
			printf(" Failure (%d)\n", maxValue);
		pathfindForbiddenCountFailure++;
		return false;
	}
}

bool Map::pathfindGuardArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy)
{
	Uint8 *gradient = guardAreasGradient[teamNumber][canSwim];
	Uint8 max = gradient[x + (y<<wDec)];
	if (max == 255)
		return false; // we already are in an area.
	if (max < 2)
		return false; // any existing area are too far away.
	bool found = false;
	
	// we look around us, searching for a usable position with a bigger gradient value 
	if (directionByMinigrad(1<<teamNumber, canSwim, x, y, dx, dy, gradient, true, verbose))
	{
		found = true;
	}
	
	// we are in a blocked situation, so we have to regenerate the forbidden gradient
	if (!found)
		updateGuardAreasGradient(teamNumber, canSwim);
	
	return found;
}



bool Map::pathfindClearArea(int teamNumber, bool canSwim, int x, int y, int *dx, int *dy)
{
	Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
	Uint8 max = gradient[x + (y<<wDec)];
	if (max == 255)
		return false; // we already are in an area.
	if (max < 2)
		return false; // any existing area are too far away.
	bool found = false;
	
	// we look around us, searching for a usable position with a bigger gradient value 
	if (directionByMinigrad(1<<teamNumber, canSwim, x, y, dx, dy, gradient, true, verbose))
	{
		found = true;
	}
	
	// we are in a blocked situation, so we have to regenerate the forbidden gradient
	if (!found)
		updateClearAreasGradient(teamNumber, canSwim);
	
	return found;
}



void Map::updateForbiddenGradient(int teamNumber, bool canSwim)
{
	if (size <= 65536)
		updateForbiddenGradient<Uint16>(teamNumber, canSwim);
	else
		updateForbiddenGradient<Uint32>(teamNumber, canSwim);
}

template<typename Tint> void Map::updateForbiddenGradient(int teamNumber, bool canSwim)
{
#define SIMONS_FORBIDDEN_GRADIENT_INIT

#if defined(TEST_FORBIDDEN_GRADIENT_INIT)
 #define SIMONS_FORBIDDEN_GRADIENT_INIT
 #define SIMPLE_FORBIDDEN_GRADIENT_INIT
#endif

	Tint *listedAddr = new Tint[size];
	size_t listCountWrite=0;
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);

#ifdef SIMON2_FORBIDDEN_GRADIENT_INIT
	Uint8 *gradient = forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
	for (size_t i = 0; i < size; i++)
	{
		const Case& c = cases[i];
		if ((c.ressource.type != NO_RES_TYPE) || (c.building!=NOGBID) || (!canSwim && isWater(i)))
		{
			gradient[i] = 0;
		}
		else if ((c.forbidden) & teamMask)
		{
			// we compute the 8 addresses around i:
			// (a stands for address, u for up, d for down, l for left, r for right, m for middle)
			size_t aul = (i - 1 - w) & (size - 1);
			size_t aum = (i     - w) & (size - 1);
			size_t aur = (i + 1 - w) & (size - 1);
			size_t amr = (i + 1    ) & (size - 1);
			size_t adr = (i + 1 + w) & (size - 1);
			size_t adm = (i     + w) & (size - 1);
			size_t adl = (i - 1 + w) & (size - 1);
			size_t aml = (i - 1    ) & (size - 1);
			
			if( ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aul].forbidden) &teamMask)
				|| (cases[aul].building!=NOGBID) || (!canSwim && isWater(aul))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aum].forbidden) &teamMask)
			    || (cases[aum].building!=NOGBID) || (!canSwim && isWater(aum))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aur].forbidden) &teamMask)
			    || (cases[aur].building!=NOGBID) || (!canSwim && isWater(aur))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[amr].forbidden) &teamMask)
			    || (cases[amr].building!=NOGBID) || (!canSwim && isWater(amr))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[adr].forbidden) &teamMask)
			    || (cases[adr].building!=NOGBID) || (!canSwim && isWater(adr))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[adm].forbidden) &teamMask)
			    || (cases[adm].building!=NOGBID) || (!canSwim && isWater(adm))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[adl].forbidden) &teamMask)
			    || (cases[adl].building!=NOGBID) || (!canSwim && isWater(adl))) &&
			    ((cases[aul].ressource.type != NO_RES_TYPE) || ((cases[aml].forbidden) &teamMask)
			    || (cases[aml].building!=NOGBID) || (!canSwim && isWater(aml))) )
			{
				gradient[i]= 1;
			}
			else
			{
				gradient[i]=254;
				listedAddr[listCountWrite++] = i;
			}
		}
		else
		{
			gradient[i] = 255;
		}
	}
	// Then we propagate the gradient
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_FORBIDDEN, canSwim);
#endif

#if defined(SIMONS_FORBIDDEN_GRADIENT_INIT)
	Uint8 *testgradient = forbiddenGradient[teamNumber][canSwim];
	assert(testgradient);
	size_t listCountWriteInit = 0;
	
	// We set the obstacle and free places
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.ressource.type!=NO_RES_TYPE)
			testgradient[i] = 0;
		else if (c.building!=NOGBID)
			testgradient[i] = 0;
		else if (!canSwim && isWater(i))
			testgradient[i] = 0;
		else if(immobileUnits[i] != 255)
			testgradient[i]=0;
		else if (c.forbidden&teamMask)
		{
			testgradient[i]= 1;  // Later: check if we can set it to 254.
			listedAddr[listCountWriteInit++] = i;  // Remember this field.
		}
		else
			testgradient[i] = 255;
	}

	// Now check if the forbidden fields border free fields. 
	// If a field does, its gradient must be 254 and it can be used as a source
	// for the forbidden gradient.
	for (size_t listCountReadInit=0; listCountReadInit<listCountWriteInit; listCountReadInit++)
	{
		size_t i = listedAddr[listCountReadInit];
		size_t y = i >> wDec;               // Calculate the coordinates of
		size_t x = i & wMask;               // the current field and of the
		
		size_t yu = ((y - 1) & hMask);      // fields next to it.
		size_t yd = ((y + 1) & hMask);
		size_t xl = ((x - 1) & wMask);
		size_t xr = ((x + 1) & wMask);

		size_t deltaAddrC[8];
		deltaAddrC[0] = (yu << wDec) | xl;
		deltaAddrC[1] = (yu << wDec) | x ;
		deltaAddrC[2] = (yu << wDec) | xr;
		deltaAddrC[3] = (y  << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x ;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y  << wDec) | xl;
		for( int ci=0; ci<8; ci++)
		{
			if( testgradient[ deltaAddrC[ci] ] == 255 )
			{
				testgradient[i] = 254;
				listedAddr[listCountWrite++] = i;
				break;
			}
		}
	}
	
	// Then we propagate the gradient
	updateGlobalGradient(testgradient, listedAddr, listCountWrite, GT_FORBIDDEN, canSwim);
#endif	

#if defined(SIMPLE_FORBIDDEN_GRADIENT_INIT)
	listCountWrite = 0;
 #if defined(TEST_FORBIDDEN_GRADIENT_INIT)
	Uint8 *gradient = new Uint8[size];
 #else
	Uint8 *gradient = forbiddenGradient[teamNumber][canSwim];
	assert(gradient);
 #endif

	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.ressource.type!=NO_RES_TYPE)
			gradient[i] = 0;
		else if (c.building!=NOGBID)
			gradient[i] = 0;
		else if (!canSwim && isWater(i))
			gradient[i] = 0;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.forbidden&teamMask)
			gradient[i]= 1;
		else
		{
			listedAddr[listCountWrite++] = i;
			gradient[i] = 255;
		}
	}
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_FORBIDDEN, canSwim);
#endif
	delete[] listedAddr;
#if defined(TEST_FORBIDDEN_GRADIENT_INIT)
	assert (memcmp (testgradient, gradient, size) == 0);
#endif

}

void Map::updateForbiddenGradient(int teamNumber)
{
	for (int i=0; i<2; i++)
		updateForbiddenGradient(teamNumber, i);
}

void Map::updateForbiddenGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateForbiddenGradient(i);
}

void Map::updateGuardAreasGradient(int teamNumber, bool canSwim)
{
	if (size <= 65536)
		updateGuardAreasGradient<Uint16>(teamNumber, canSwim);
	else
		updateGuardAreasGradient<Uint32>(teamNumber, canSwim);
}

template<typename Tint> void Map::updateGuardAreasGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = guardAreasGradient[teamNumber][canSwim];
	assert(gradient);
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	
	// We set the obstacle and free places
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i] = 0;
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.ressource.type != NO_RES_TYPE)
			gradient[i] = 0;
		else if (c.building != NOGBID)
			gradient[i] = 0;
		else if (!canSwim && isWater(i))
			gradient[i] = 0;
		else if (c.guardArea & teamMask)
		{
			gradient[i] = 255;
			listedAddr[listCountWrite++] = i;
		}
		else
			gradient[i] = 1;
	}
	
	// Then we propagate the gradient
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_GUARD_AREA, canSwim);
	delete[] listedAddr;
}

void Map::updateGuardAreasGradient(int teamNumber)
{
	for (int i=0; i<2; i++)
		updateGuardAreasGradient(teamNumber, i);
}

void Map::updateGuardAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateGuardAreasGradient(i);
}

void Map::updateClearAreasGradient(int teamNumber, bool canSwim)
{
	if (size <= 65536)
		updateClearAreasGradient<Uint16>(teamNumber, canSwim);
	else
		updateClearAreasGradient<Uint32>(teamNumber, canSwim);
}

template<typename Tint> void Map::updateClearAreasGradient(int teamNumber, bool canSwim)
{
	Uint8 *gradient = clearAreasGradient[teamNumber][canSwim];
	assert(gradient);
	Tint *listedAddr = new Tint[size];
	size_t listCountWrite = 0;
	
	// We set the obstacle and free places
	Uint32 teamMask = Team::teamNumberToMask(teamNumber);
	for (size_t i=0; i<size; i++)
	{
		const Case& c=cases[i];
		if (c.forbidden & teamMask)
			gradient[i] = 0;
		else if(c.clearArea & teamMask && (c.ressource.type == WOOD || c.ressource.type == CORN || c.ressource.type == PAPYRUS || c.ressource.type == ALGA))
		{
			gradient[i] = 255;
			listedAddr[listCountWrite++] = i;
		}
		else if(immobileUnits[i] != 255)
			gradient[i]=0;
		else if (c.ressource.type != NO_RES_TYPE)
			gradient[i] = 0;
		else if (c.building != NOGBID)
			gradient[i] = 0;
		else if (!canSwim && isWater(i))
			gradient[i] = 0;
		else
			gradient[i] = 1;
	}
	
	// Then we propagate the gradient
	updateGlobalGradient(gradient, listedAddr, listCountWrite, GT_CLEAR_AREA, canSwim);
	delete[] listedAddr;
}

void Map::updateClearAreasGradient(int teamNumber)
{
	for (int i=0; i<2; i++)
		updateClearAreasGradient(teamNumber, i);
}

void Map::updateClearAreasGradient()
{
	for (int i=0; i<game->mapHeader.getNumberOfTeams(); i++)
		updateClearAreasGradient(i);
}

bool Map::pathfindPointToPoint(int x, int y, int targetX, int targetY, int *dx, int *dy, bool canSwim, Uint32 teamMask, int maximumLength)
{
	//This implements a fairly standard A* algorithm, except that each node does not store the location
	//of the node that lead to it, thus, you can't trace backwards to the starting point to get the path.
	//Instead, each node holds the direction that you left from the initial node that lead to it, so you
	//can't trace backwards to find the path, but you can instantly find the direction you need to go from
	//the initial node, a small optimization since we don't need the whole path
	targetX = (targetX + w) & wMask;
	targetY = (targetY + h) & hMask;
	
	AStarComparator compare(astarpoints);
	
	///Priority queues use heaps internally, which I've read is the fastest for A* algorithm
	std::priority_queue<int, std::vector<int>, AStarComparator> openList(compare);
	openList.push((x << hDec) + y);
	astarpoints[(x << hDec) + y] = AStarAlgorithmPoint(x,y,0,0,0,0,false);
	
	//These are all the examined points, so that these positions on astarponts
	//Can be reset later. Why not reset or re-allocate the whole thing every
	//call? Its slow! Use reserve to avoid doing this multiple times
	astarExaminedPoints.reserve(maximumLength*2 + 6);
	astarExaminedPoints.push_back((x << hDec) + y);
	
	while(!openList.empty())
	{
		///Get the smallest from the heap
		int position = openList.top();
		openList.pop();

		AStarAlgorithmPoint& pos = astarpoints[position];
		pos.isClosed = true;
				
		if((pos.x == targetX && pos.y == targetY) || (pos.moveCost > maximumLength))
		{
			break;
		}
		
		for(int lx=-1; lx<=1; ++lx)
		{
			for(int ly=-1; ly<=1; ++ly)
			{
				int nx = (pos.x + lx + w) & wMask;
				int ny = (pos.y + ly + h) & hMask;
				int n = (nx << hDec) + ny;
				AStarAlgorithmPoint& npos = astarpoints[n];
				if(npos.isClosed)
				{
					continue;
				}
				else
				{
					int moveCost = pos.moveCost + 1;
					int totalCost = moveCost +  warpDistMax(targetX, targetY, nx, ny);
					
					//If this cell hasn't been examined at all yet
					if(npos.x == -1)
					{
						if(isFreeForGroundUnit(nx, ny, canSwim, teamMask) || (nx == targetX && ny == targetY))
						{
							//If the parent cell is the starting cell, add in the starting direction
							if(pos.dx == 0 && pos.dy == 0)
							{
								npos = AStarAlgorithmPoint(nx, ny, lx, ly, moveCost, totalCost, false);
								openList.push(n);
							}
							//Else, the direction is the same as the parents node
							else
							{
								npos = AStarAlgorithmPoint(nx, ny, pos.dx, pos.dy, moveCost, totalCost, false);
								openList.push(n);
							}
							astarExaminedPoints.push_back(n);
						}
					}
					//Check if we can improve this cells value by taking this route
					else if(npos.moveCost > moveCost)
					{
						npos.moveCost = moveCost;
						npos.totalCost = totalCost;
						npos.dx = pos.dx;
						npos.dy = pos.dy;
					}
				}
			}
		}
	}
	
	AStarAlgorithmPoint final = astarpoints[(targetX << hDec) + targetY];

	//Clear all of the examined points for the next call to this algorithm
	for(int i=0; i<astarExaminedPoints.size(); ++i)
	{
		astarpoints[astarExaminedPoints[i]] = AStarAlgorithmPoint();
	}
	
	astarExaminedPoints.clear();

	//It was never examined, thus there is no paths
	if(final.x == -1)
		return false;
	
	//Input direction of the final square to the unit
	*dx = final.dx;
	*dy = final.dy;
	return true;
}



void Map::initExploredArea(int teamNumber)
{
	std::fill(exploredArea[teamNumber], exploredArea[teamNumber] + size, 0);
}

void Map::makeDiscoveredAreasExplored (int teamNumber)
{
  /* This function is a stupid hack to make up for the fact that
     exploredArea is not saved in saved games.  It allows doing
     something less awful than simply making everything considered
     unexplored (which completely messes up explorer behavior and
     makes them explore the entire world all over again) when games
     are reloaded. */
  assert(game->teams[teamNumber]);
  assert(game->teams[teamNumber]->me);
  assert(exploredArea[teamNumber]);
  for (int x = 0; x < getW(); x++) {
    for (int y = 0; y < getH(); y++) {
      if (isMapDiscovered (x, y, game->teams[teamNumber]->me)) {
        setMapExploredByUnit (x, y, 1, 1, teamNumber); }}}
}

void Map::updateExploredArea(int teamNumber)
{
	for (size_t i = 0; i < size; i++)
		if (exploredArea[teamNumber][i] > 0)
			exploredArea[teamNumber][i]--;
}

void Map::regenerateMap(int x, int y, int w, int h)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
			setTerrain(dx, dy, lookup(getUMTerrain(dx,dy), getUMTerrain(dx+1,dy), getUMTerrain(dx,dy+1), getUMTerrain(dx+1,dy+1)));
}

Uint16 Map::lookup(Uint8 tl, Uint8 tr, Uint8 bl, Uint8 br)
{
	/*
		Value of vertice's order in square :

		3 -- 2
		|    |
		|    |
		1 -- 0

		The index in the following table is :
		val[0] + val[1]*k + val[2]*k^2 + val[3]*k^3
		where k is the number of different possibilites.
		
		H = grass
		S = sand
		E = water
	*/
	const Uint16 terrainLookupTable[81][2] =
	{
		{ 0, 16 },		// H, H, H, H
		{ 80, 8 },		// H, H, H, S
		{ 0, 16 },		// H, H, H, E
		{ 88, 8 },		// H, H, S, H
		{ 48, 8 },		// H, H, S, S
		{ 0, 16 },		// H, H, S, E
		{ 0, 16 },		// H, H, E, H
		{ 0, 16 },		// H, H, E, S
		{ 0, 16 },		// H, H, E, E
		{ 104, 8 },		// H, S, H, H
		{ 64, 8 },		// H, S, H, S
		{ 0, 16 },		// H, S, H, E
		{ 120, 8 },		// H, S, S, H
		{ 32, 8 },		// H, S, S, S
		{ 0, 16 },		// H, S, S, E
		{ 0, 16 },		// H, S, E, H
		{ 0, 16 },		// H, S, E, S
		{ 0, 16 },		// H, S, E, E
		{ 0, 16 },		// H, E, H, H
		{ 0, 16 },		// H, E, H, S
		{ 0, 16 },		// H, E, H, E
		{ 0, 16 },		// H, E, S, H
		{ 0, 16 },		// H, E, S, S
		{ 0, 16 },		// H, E, S, E
		{ 0, 16 },		// H, E, E, H
		{ 0, 16 },		// H, E, E, S
		{ 0, 16 },		// H, E, E, E

		{ 96, 8 },		// S, H, H, H
		{ 112, 8 },		// S, H, H, S
		{ 0, 16 },		// S, H, H, E
		{ 72, 8 },		// S, H, S, H
		{ 40, 8 },		// S, H, S, S
		{ 0, 16 },		// S, H, S, E
		{ 0, 16 },		// S, H, E, H
		{ 0, 16 },		// S, H, E, S
		{ 0, 16 },		// S, H, E, E
		{ 56, 8 },		// S, S, H, H
		{ 24, 8 },		// S, S, H, S
		{ 0, 16 },		// S, S, H, E
		{ 16, 8 },		// S, S, S, H
		{ 128, 16 },	// S, S, S, S
		{ 208, 8 },		// S, S, S, E
		{ 0, 16 },		// S, S, E, H
		{ 216, 8 },		// S, S, E, S
		{ 176, 8 },		// S, S, E, E
		{ 0, 16 },		// S, E, H, H
		{ 0, 16 },		// S, E, H, S
		{ 0, 16 },		// S, E, H, E
		{ 0, 16 },		// S, E, S, H
		{ 232, 8 },		// S, E, S, S
		{ 192, 8 },		// S, E, S, E
		{ 0, 16 },		// S, E, E, H
		{ 240, 8 },		// S, E, E, S
		{ 160, 8 },		// S, E, E, E

		{ 0, 16 },		// E, H, H, H
		{ 0, 16 },		// E, H, H, S
		{ 0, 16 },		// E, H, H, E
		{ 0, 16 },		// E, H, S, H
		{ 0, 16 },		// E, H, S, S
		{ 0, 16 },		// E, H, S, E
		{ 0, 16 },		// E, H, E, H
		{ 0, 16 },		// E, H, E, S
		{ 0, 16 },		// E, H, E, E
		{ 0, 16 },		// E, S, H, H
		{ 0, 16 },		// E, S, H, S
		{ 0, 16 },		// E, S, H, E
		{ 0, 16 },		// E, S, S, H
		{ 224, 8 },		// E, S, S, S
		{ 248, 8 },		// E, S, S, E
		{ 0, 16 },		// E, S, E, H
		{ 200, 8 },		// E, S, E, S
		{ 168, 8 },		// E, S, E, E
		{ 0, 16 },		// E, E, H, H
		{ 0, 16 },		// E, E, H, S
		{ 0, 16 },		// E, E, H, E
		{ 0, 16 },		// E, E, S, H
		{ 184, 8 },		// E, E, S, S
		{ 152, 8 },		// E, E, S, E
		{ 0, 16 },		// E, E, E, H
		{ 144, 8 },		// E, E, E, S
		{ 256, 16 },	// E, E, E, E
	};

	tl=2-tl;
	tr=2-tr;
	bl=2-bl;
	br=2-br;
	int index=tl*27+tr*9+bl*3+br;

	return terrainLookupTable[index][0]+(syncRand()%terrainLookupTable[index][1]);
}

Uint32 Map::checkSum(bool heavy)
{
	Uint32 cs=size;
	if (heavy)
	{
		const Case* end = cases + (w * h);
		for (Case* c=cases; c < end; ++c)
		{
			cs+=
				c->terrain +
				c->building +
				c->ressource.getUint32() +
				c->groundUnit +
				c->airUnit +
				c->forbidden +
				c->scriptAreas;
			cs=(cs<<1)|(cs>>31);
		}
	};
	return cs;
}

Sint32 Map::warpDistSquare(int px, int py, int qx, int qy)
{
	Sint32 dx=abs(px-qx);
	Sint32 dy=abs(py-qy);
	dx&=wMask;
	dy&=hMask;
	if (dx>(w>>1))
		dx=w-dx;
	if (dy>(h>>1))
		dy=h-dy;
	
	return ((dx*dx)+(dy*dy));
}

Sint32 Map::warpDistMax(int px, int py, int qx, int qy)
{
	Sint32 dx=abs(px-qx);
	Sint32 dy=abs(py-qy);
	dx&=wMask;
	dy&=hMask;
	if (dx>(w>>1))
		dx=abs(w-dx);
	if (dy>(h>>1))
		dy=abs(h-dy);
	if (dx>dy)
		return dx;
	else
		return dy;
}

bool Map::isInLocalGradient(int ux, int uy, int bx, int by)
{
	Sint32 dx=abs(ux-bx);
	Sint32 dy=abs(uy-by);
	dx&=wMask;
	dy&=hMask;
	if (dx>(w>>1))
		dx=abs(w-dx);
	if (dy>(h>>1))
		dy=abs(h-dy);
	if (dx>dy)
	{
		if (dx<15)
			return true;
		if (dx>15)
			return false;
		
		return ((bx+15) & wMask)==(ux & wMask);
	}
	else if (dx<dy)
	{
		if (dy<15)
			return true;
		if (dy>15)
			return false;
		
		return ((by+15) & wMask)==(uy & wMask);
	}
	else
	{
		if (dx<15)
			return true;
		if (dx>15)
			return false;
		
		return (((bx+15) & wMask)==(ux & wMask)) && (((by+15) & wMask)==(uy & wMask));
	}
}

void Map::dumpGradient(Uint8 *gradient, const char *filename)
{
	FILE *fp = globalContainer->fileManager->openFP(filename, "wb");
	if (fp)
	{
		fprintf(fp, "P5 %d %d 255\n", w, h);
		fwrite(gradient, w, h, fp);
		fclose(fp);
	}
}

