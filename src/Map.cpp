/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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

Map::Map()
{
	game=NULL;

	arraysBuilt=false;
	
	mapDiscovered=NULL;
	fogOfWar=NULL;
	fogOfWarA=NULL;
	fogOfWarB=NULL;
	cases=NULL;
	for (int t=0; t<32; t++)
		for (int r=0; r<MAX_NB_RESSOURCES; r++)
			for (int s=0; s<2; s++)
			{
				ressourcesGradient[t][r][s]=NULL;
				gradientUpdatedDepth[t][r][s]=0;
			}
	undermap=NULL;
	sectors=NULL;
	
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
	
	//Gradients stats:
	for (int t=0; t<16; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
		{
			ressourceAviableCount[t][r]=0;
			ressourceAviableCountFast[t][r]=0;
			ressourceAviableCountFar[t][r]=0;
			ressourceAviableCountSuccess[t][r]=0;
			ressourceAviableCountFailureBase[t][r]=0;
			ressourceAviableCountFailureOvercount[t][r]=0;
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
	pathToBuildingCountFarOldSuccess=0;
	pathToBuildingCountFarOldFailureLocked=0;
	pathToBuildingCountFarOldFailureBad=0;
	pathToBuildingCountFarOldFailureUnusable=0;
	pathToBuildingCountFarUpdateSuccess=0;
	pathToBuildingCountFarUpdateSuccessAround=0;
	pathToBuildingCountFarUpdateFailureLocked=0;
	pathToBuildingCountFarUpdateFailureVirtual=0;
	pathToBuildingCountFarUpdateFailureBad=0;
	
	localBuildingGradientUpdate=0;
	localBuildingGradientUpdateLocked=0;
	globalBuildingGradientUpdate=0;
	globalBuildingGradientUpdateLocked=0;
	
	buildingAviableCountTot=0;
	buildingAviableCountClose=0;
	buildingAviableCountCloseSuccess=0;
	buildingAviableCountCloseSuccessAround=0;
	buildingAviableCountCloseSuccessUpdate=0;
	buildingAviableCountCloseSuccessUpdateAround=0;
	buildingAviableCountCloseFailureLocked=0;
	buildingAviableCountCloseFailureEnd=0;
	
	buildingAviableCountIsFar=0;
	buildingAviableCountFar=0;
	buildingAviableCountFarNew=0;
	buildingAviableCountFarNewSuccess=0;
	buildingAviableCountFarNewSuccessClosely=0;
	buildingAviableCountFarNewFailureLocked=0;
	buildingAviableCountFarNewFailureVirtual=0;
	buildingAviableCountFarNewFailureEnd=0;
	buildingAviableCountFarOld=0;
	buildingAviableCountFarOldSuccess=0;
	buildingAviableCountFarOldSuccessAround=0;
	buildingAviableCountFarOldFailureLocked=0;
	buildingAviableCountFarOldFailureEnd=0;
	
	logFile = globalContainer->logFileManager->getFile("Map.log");
}

Map::~Map(void)
{
	clear();
}

void Map::clear()
{
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
						ressourcesGradient[t][r][s]=NULL;
					}
		
		assert(undermap);
		delete[] undermap;
		undermap=NULL;
		
		assert(sectors);
		delete[] sectors;
		sectors=NULL;

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
		assert(undermap==NULL);
		assert(sectors==NULL);

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
				gradientUpdatedDepth[t][r][s]=0;
	
	
	fprintf(logFile, "\n");
	if (game)
		for (int t=0; t<16; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				if (ressourceAviableCount[t][r])
				{
					fprintf(logFile, "ressourceAviableCount[%d][%d]=%d\n", t, r,
						ressourceAviableCount[t][r]);
					fprintf(logFile, "| ressourceAviableCountFast[%d][%d]=%d (%f %%)\n", t, r,
						ressourceAviableCountFast[t][r],
						100.*(double)ressourceAviableCountFast[t][r]/(double)ressourceAviableCount[t][r]);
					fprintf(logFile, "+ ressourceAviableCountFar[%d][%d]=%d (%f ratio)\n", t, r,
						ressourceAviableCountFar[t][r],
						100.*(double)ressourceAviableCountFar[t][r]/(double)ressourceAviableCount[t][r]);
					fprintf(logFile, "| ressourceAviableCountSuccess[%d][%d]=%d (%f %%)\n", t, r,
						ressourceAviableCountSuccess[t][r],
						100.*(double)ressourceAviableCountSuccess[t][r]/(double)ressourceAviableCount[t][r]);
					int ressourceAviableCountFailure=ressourceAviableCountFailureBase[t][r]+ressourceAviableCountFailureOvercount[t][r];
					fprintf(logFile, "| ressourceAviableCountFailure[%d][%d]=%d (%f %%)\n", t, r,
						ressourceAviableCountFailure,
						100.*(double)ressourceAviableCountFailure/(double)ressourceAviableCount[t][r]);
					fprintf(logFile, "|- ressourceAviableCountFailureBase[%d][%d]=%d (%f %%) (%f %% of failure)\n", t, r,
						ressourceAviableCountFailureBase[t][r],
						100.*(double)ressourceAviableCountFailureBase[t][r]/(double)ressourceAviableCount[t][r],
						100.*(double)ressourceAviableCountFailureBase[t][r]/(double)ressourceAviableCountFailure);
					fprintf(logFile, "|- ressourceAviableCountFailureOvercount[%d][%d]=%d (%f %%) (%f %% of failure)\n", t, r,
						ressourceAviableCountFailureOvercount[t][r],
						100.*(double)ressourceAviableCountFailureOvercount[t][r]/(double)ressourceAviableCount[t][r],
						100.*(double)ressourceAviableCountFailureOvercount[t][r]/(double)ressourceAviableCountFailure);
				}
	
	for (int t=0; t<16; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
		{
			ressourceAviableCount[t][r]=0;
			ressourceAviableCountFast[t][r]=0;
			ressourceAviableCountFar[t][r]=0;
			ressourceAviableCountSuccess[t][r]=0;
			ressourceAviableCountFailureBase[t][r]=0;
			ressourceAviableCountFailureOvercount[t][r]=0;
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

		assert(pathToBuildingCountFar==
			+pathToBuildingCountFarOldSuccess
			+pathToBuildingCountFarOldFailureLocked
			+pathToBuildingCountFarOldFailureBad
			//+pathToBuildingCountFarOldFailureUnusable
			+pathToBuildingCountFarUpdateSuccess
			+pathToBuildingCountFarUpdateSuccessAround
			+pathToBuildingCountFarUpdateFailureLocked
			+pathToBuildingCountFarUpdateFailureVirtual
			+pathToBuildingCountFarUpdateFailureBad);
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
			+pathToBuildingCountFarOldFailureUnusable;
		fprintf(logFile, "|-  pathToBuildingCountFarOld=%d (%f %% of tot) (%f %% of far))\n",
			pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOld/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOld/(double)pathToBuildingCountFar);
		
		fprintf(logFile, "|-   pathToBuildingCountFarOldSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of old))\n",
			pathToBuildingCountFarOldSuccess,
			100.*(double)pathToBuildingCountFarOldSuccess/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldSuccess/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldSuccess/(double)pathToBuildingCountFarOld);
		
		int pathToBuildingCountFarOldFailure=
			+pathToBuildingCountFarOldFailureLocked
			+pathToBuildingCountFarOldFailureBad
			+pathToBuildingCountFarOldFailureUnusable;
		fprintf(logFile, "|-   pathToBuildingCountFarOldFailure=%d (%f %% of tot) (%f %% of far) (%f %% of old))\n",
			pathToBuildingCountFarOldFailure,
			100.*(double)pathToBuildingCountFarOldFailure/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailure/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailure/(double)pathToBuildingCountFarOld);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure))\n",
			pathToBuildingCountFarOldFailureLocked,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureLocked/(double)pathToBuildingCountFarOldFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureBad=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure))\n",
			pathToBuildingCountFarOldFailureBad,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureBad/(double)pathToBuildingCountFarOldFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarOldFailureUnusable=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure))\n",
			pathToBuildingCountFarOldFailureUnusable,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountFarOld,
			100.*(double)pathToBuildingCountFarOldFailureUnusable/(double)pathToBuildingCountFarOldFailure);
		
		int pathToBuildingCountFarUpdate=
			+pathToBuildingCountFarUpdateSuccess
			+pathToBuildingCountFarUpdateSuccessAround
			+pathToBuildingCountFarUpdateFailureLocked
			+pathToBuildingCountFarUpdateFailureVirtual
			+pathToBuildingCountFarUpdateFailureBad;
		fprintf(logFile, "|-  pathToBuildingCountFarUpdate=%d (%f %% of tot) (%f %% of far))\n",
			pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdate/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdate/(double)pathToBuildingCountFar);
		fprintf(logFile, "|-   pathToBuildingCountFarUpdateSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of update))\n",
			pathToBuildingCountFarUpdateSuccess,
			100.*(double)pathToBuildingCountFarUpdateSuccess/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateSuccess/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateSuccess/(double)pathToBuildingCountFarUpdate);
		fprintf(logFile, "|-   pathToBuildingCountFarUpdateSuccessAround=%d (%f %% of tot) (%f %% of far) (%f %% of update))\n",
			pathToBuildingCountFarUpdateSuccessAround,
			100.*(double)pathToBuildingCountFarUpdateSuccessAround/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateSuccessAround/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateSuccessAround/(double)pathToBuildingCountFarUpdate);
		
		int pathToBuildingCountFarUpdateFailure=
			+pathToBuildingCountFarUpdateFailureLocked
			+pathToBuildingCountFarUpdateFailureVirtual
			+pathToBuildingCountFarUpdateFailureBad;
		fprintf(logFile, "|-   pathToBuildingCountFarUpdateFailure=%d (%f %% of tot) (%f %% of far) (%f %% of update))\n",
			pathToBuildingCountFarUpdateFailure,
			100.*(double)pathToBuildingCountFarUpdateFailure/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailure/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailure/(double)pathToBuildingCountFarUpdate);
		fprintf(logFile, "|-    pathToBuildingCountFarUpdateFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of update) (%f %% of failure))\n",
			pathToBuildingCountFarUpdateFailureLocked,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdateFailureLocked/(double)pathToBuildingCountFarUpdateFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarUpdateFailureVirtual=%d (%f %% of tot) (%f %% of far) (%f %% of update) (%f %% of failure))\n",
			pathToBuildingCountFarUpdateFailureVirtual,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountTot,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountFar,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountFarUpdate,
			100.*(double)pathToBuildingCountFarUpdateFailureVirtual/(double)pathToBuildingCountFarUpdateFailure);
		fprintf(logFile, "|-    pathToBuildingCountFarUpdateFailureBad=%d (%f %% of tot) (%f %% of far) (%f %% of update) (%f %% of failure))\n",
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
	
	pathToBuildingCountFarOldSuccess=0;
	pathToBuildingCountFarOldFailureLocked=0;
	pathToBuildingCountFarOldFailureBad=0;
	pathToBuildingCountFarOldFailureUnusable=0;
	
	pathToBuildingCountFarUpdateSuccess=0;
	pathToBuildingCountFarUpdateSuccessAround=0;
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
	fprintf(logFile, "buildingAviableCountTot=%d\n", buildingAviableCountTot);
	if (buildingAviableCountTot)
	{
		fprintf(logFile, "|- buildingAviableCountClose=%d (%f %%)\n",
			buildingAviableCountClose,
			100.*(double)buildingAviableCountClose/(double)buildingAviableCountTot);
		fprintf(logFile, "|-  buildingAviableCountCloseSuccess=%d (%f %% of tot) (%f %% of close)\n",
			buildingAviableCountCloseSuccess,
			100.*(double)buildingAviableCountCloseSuccess/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseSuccess/(double)buildingAviableCountClose);
		fprintf(logFile, "|-  buildingAviableCountCloseSuccessAround=%d (%f %% of tot) (%f %% of close)\n",
			buildingAviableCountCloseSuccessAround,
			100.*(double)buildingAviableCountCloseSuccessAround/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseSuccessAround/(double)buildingAviableCountClose);
		fprintf(logFile, "|-  buildingAviableCountCloseSuccessUpdate=%d (%f %% of tot) (%f %% of close)\n",
			buildingAviableCountCloseSuccessUpdate,
			100.*(double)buildingAviableCountCloseSuccessUpdate/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseSuccessUpdate/(double)buildingAviableCountClose);
		fprintf(logFile, "|-  buildingAviableCountCloseSuccessUpdateAround=%d (%f %% of tot) (%f %% of close)\n",
			buildingAviableCountCloseSuccessUpdateAround,
			100.*(double)buildingAviableCountCloseSuccessUpdateAround/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseSuccessUpdateAround/(double)buildingAviableCountClose);
		
		int buildingAviableCountCloseFailure=
			+buildingAviableCountCloseFailureLocked
			+buildingAviableCountCloseFailureEnd;
		fprintf(logFile, "|-  buildingAviableCountCloseFailure=%d (%f %% of tot) (%f %% of close)\n",
			buildingAviableCountCloseFailure,
			100.*(double)buildingAviableCountCloseFailure/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseFailure/(double)buildingAviableCountClose);
		fprintf(logFile, "|-   buildingAviableCountCloseFailureLocked=%d (%f %% of tot) (%f %% of close) (%f %% of failure)\n",
			buildingAviableCountCloseFailureLocked,
			100.*(double)buildingAviableCountCloseFailureLocked/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseFailureLocked/(double)buildingAviableCountClose,
			100.*(double)buildingAviableCountCloseFailureLocked/(double)buildingAviableCountCloseFailure);
		fprintf(logFile, "|-   buildingAviableCountCloseFailureEnd=%d (%f %% of tot) (%f %% of close) (%f %% of failure)\n",
			buildingAviableCountCloseFailureEnd,
			100.*(double)buildingAviableCountCloseFailureEnd/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountCloseFailureEnd/(double)buildingAviableCountClose,
			100.*(double)buildingAviableCountCloseFailureEnd/(double)buildingAviableCountCloseFailure);
		
		fprintf(logFile, "|- buildingAviableCountFar=%d (%f %%)\n",
			buildingAviableCountFar,
			100.*(double)buildingAviableCountFar/(double)buildingAviableCountTot);
		fprintf(logFile, "|-  buildingAviableCountIsFar=%d (%f %% of tot) (%f %% of far)\n",
			buildingAviableCountIsFar,
			100.*(double)buildingAviableCountIsFar/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountIsFar/(double)buildingAviableCountFar);
		
		fprintf(logFile, "|-  buildingAviableCountFarOld=%d (%f %% of tot) (%f %% of far)\n",
			buildingAviableCountFarOld,
			100.*(double)buildingAviableCountFarOld/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarOld/(double)buildingAviableCountFar);
		fprintf(logFile, "|-   buildingAviableCountFarOldSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			buildingAviableCountFarOldSuccess,
			100.*(double)buildingAviableCountFarOldSuccess/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarOldSuccess/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarOldSuccess/(double)buildingAviableCountFarOld);
		fprintf(logFile, "|-   buildingAviableCountFarOldSuccessAround=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			buildingAviableCountFarOldSuccessAround,
			100.*(double)buildingAviableCountFarOldSuccessAround/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarOldSuccessAround/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarOldSuccessAround/(double)buildingAviableCountFarOld);
		
		int buildingAviableCountFarOldFailure=
			+buildingAviableCountFarOldFailureLocked
			+buildingAviableCountFarOldFailureEnd;
		fprintf(logFile, "|-   buildingAviableCountFarOldFailure=%d (%f %% of tot) (%f %% of far) (%f %% of old)\n",
			buildingAviableCountFarOldFailure,
			100.*(double)buildingAviableCountFarOldFailure/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarOldFailure/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarOldFailure/(double)buildingAviableCountFarOld);
		fprintf(logFile, "|-    buildingAviableCountFarOldFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			buildingAviableCountFarOldFailureLocked,
			100.*(double)buildingAviableCountFarOldFailureLocked/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarOldFailureLocked/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarOldFailureLocked/(double)buildingAviableCountFarOld,
			100.*(double)buildingAviableCountFarOldFailureLocked/(double)buildingAviableCountFarOldFailure);
		fprintf(logFile, "|-    buildingAviableCountFarOldFailureEnd=%d (%f %% of tot) (%f %% of far) (%f %% of old) (%f %% of failure)\n",
			buildingAviableCountFarOldFailureEnd,
			100.*(double)buildingAviableCountFarOldFailureEnd/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarOldFailureEnd/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarOldFailureEnd/(double)buildingAviableCountFarOld,
			100.*(double)buildingAviableCountFarOldFailureEnd/(double)buildingAviableCountFarOldFailure);
		
		fprintf(logFile, "|-  buildingAviableCountFarNew=%d (%f %% of tot) (%f %% of far)\n",
			buildingAviableCountFarNew,
			100.*(double)buildingAviableCountFarNew/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNew/(double)buildingAviableCountFar);
		fprintf(logFile, "|-   buildingAviableCountFarNewSuccess=%d (%f %% of tot) (%f %% of far) (%f %% of new)\n",
			buildingAviableCountFarNewSuccess,
			100.*(double)buildingAviableCountFarNewSuccess/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNewSuccess/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarNewSuccess/(double)buildingAviableCountFarNew);
		fprintf(logFile, "|-   buildingAviableCountFarNewSuccessClosely=%d (%f %% of tot) (%f %% of far) (%f %% of new)\n",
			buildingAviableCountFarNewSuccess,
			100.*(double)buildingAviableCountFarNewSuccessClosely/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNewSuccessClosely/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarNewSuccessClosely/(double)buildingAviableCountFarNew);
		
		int buildingAviableCountFarNewFailure=
			+buildingAviableCountFarNewFailureLocked
			+buildingAviableCountFarNewFailureVirtual
			+buildingAviableCountFarNewFailureEnd;
		fprintf(logFile, "|-   buildingAviableCountFarNewFailure=%d (%f %% of tot) (%f %% of far) (%f %% of new)\n",
			buildingAviableCountFarNewFailure,
			100.*(double)buildingAviableCountFarNewFailure/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNewFailure/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarNewFailure/(double)buildingAviableCountFarNew);
		fprintf(logFile, "|-    buildingAviableCountFarNewFailureLocked=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of failure)\n",
			buildingAviableCountFarNewFailureLocked,
			100.*(double)buildingAviableCountFarNewFailureLocked/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNewFailureLocked/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarNewFailureLocked/(double)buildingAviableCountFarNew,
			100.*(double)buildingAviableCountFarNewFailureLocked/(double)buildingAviableCountFarNewFailure);
		fprintf(logFile, "|-    buildingAviableCountFarNewFailureVirtual=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of failure)\n",
			buildingAviableCountFarNewFailureVirtual,
			100.*(double)buildingAviableCountFarNewFailureVirtual/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNewFailureVirtual/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarNewFailureVirtual/(double)buildingAviableCountFarNew,
			100.*(double)buildingAviableCountFarNewFailureVirtual/(double)buildingAviableCountFarNewFailure);
		fprintf(logFile, "|-    buildingAviableCountFarNewFailureEnd=%d (%f %% of tot) (%f %% of far) (%f %% of new) (%f %% of failure)\n",
			buildingAviableCountFarNewFailureEnd,
			100.*(double)buildingAviableCountFarNewFailureEnd/(double)buildingAviableCountTot,
			100.*(double)buildingAviableCountFarNewFailureEnd/(double)buildingAviableCountFar,
			100.*(double)buildingAviableCountFarNewFailureEnd/(double)buildingAviableCountFarNew,
			100.*(double)buildingAviableCountFarNewFailureEnd/(double)buildingAviableCountFarNewFailure);
	}
	
	buildingAviableCountTot=0;
	
	buildingAviableCountClose=0;
	buildingAviableCountCloseSuccess=0;
	buildingAviableCountCloseSuccessAround=0;
	buildingAviableCountCloseSuccessUpdate=0;
	buildingAviableCountCloseSuccessUpdateAround=0;
	buildingAviableCountCloseFailureLocked=0;
	buildingAviableCountCloseFailureEnd=0;
	
	buildingAviableCountIsFar=0;
	buildingAviableCountFar=0;
	buildingAviableCountFarNew=0;
	buildingAviableCountFarNewSuccess=0;
	buildingAviableCountFarNewSuccessClosely=0;
	buildingAviableCountFarNewFailureLocked=0;
	buildingAviableCountFarNewFailureVirtual=0;
	buildingAviableCountFarNewFailureEnd=0;
	buildingAviableCountFarOld=0;
	buildingAviableCountFarOldSuccess=0;
	buildingAviableCountFarOldSuccessAround=0;
	buildingAviableCountFarOldFailureLocked=0;
	buildingAviableCountFarOldFailureEnd=0;	
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
	
	cases=new Case[size];

	Case initCase;
	initCase.terrain=0; // default, not really meaningfull.
	initCase.building=NOGBID;
	initCase.ressource.clear();
	initCase.groundUnit=NOGUID;
	initCase.airUnit=NOGUID;
	initCase.forbidden=0;

	undermap=new Uint8[size];
	memset(undermap, terrainType, size);
	
	for (int i=0; i<size; i++)
		cases[i]=initCase;
		
	//numberOfTeam=0, then ressourcesGradient[][][] is empty. This is done by clear();

	regenerateMap(0, 0, w, h);

	wSector=w>>4;
	hSector=h>>4;
	sizeSector=wSector*hSector;

	sectors=new Sector[sizeSector];

	arraysBuilt=true;
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
}

bool Map::load(SDL_RWops *stream, SessionGame *sessionGame, Game *game)
{
	assert(sessionGame);
	assert(sessionGame->versionMinor>=16);

	clear();

	char signature[4];
	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature, "MapB", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the beginning of Map.\n");
		return false;
	}

	// We load and compute size:
	wDec=SDL_ReadBE32(stream);
	hDec=SDL_ReadBE32(stream);
	w=1<<wDec;
	h=1<<hDec;
	wMask=w-1;
	hMask=h-1;
	size=w*h;

	// We allocate memory:
	mapDiscovered=new Uint32[size];
	fogOfWarA=new Uint32[size];
	fogOfWarB=new Uint32[size];
	fogOfWar=fogOfWarA;
	memset(fogOfWarA, 0, size*sizeof(Uint32));
	memset(fogOfWarB, 0, size*sizeof(Uint32));

	cases=new Case[size];

	undermap=new Uint8[size];

	// We read what's inside the map:
	SDL_RWread(stream, undermap, size, 1);
	for (int i=0; i<size; i++)
	{
		mapDiscovered[i]=SDL_ReadBE32(stream);

		cases[i].terrain=SDL_ReadBE16(stream);
		cases[i].building=SDL_ReadBE16(stream);

		//cases[i].ressource=SDL_ReadBE32(stream);
		SDL_RWread(stream, &(cases[i].ressource), 1, 4);

		cases[i].groundUnit=SDL_ReadBE16(stream);
		cases[i].airUnit=SDL_ReadBE16(stream);

		cases[i].forbidden=SDL_ReadBE32(stream);
	}

	if (game)
	{
		// This is a game, so we do compute gradients
		for (int t=0; t<sessionGame->numberOfTeam; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
				{
					assert(ressourcesGradient[t][r][s]==NULL);
					ressourcesGradient[t][r][s]=new Uint8[size];
					updateGradient(t, r, s, true);
				}
		for (int t=0; t<32; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<1; s++)
					gradientUpdatedDepth[t][r][s]=0;

		this->game=game;
	}

	// We load sectors:
	wSector=SDL_ReadBE32(stream);
	hSector=SDL_ReadBE32(stream);
	sizeSector=wSector*hSector;
	assert(sectors==NULL);
	sectors=new Sector[sizeSector];
	arraysBuilt=true;

	for (int i=0; i<sizeSector; i++)
		if (!sectors[i].load(stream, this->game))
			return false;

	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature, "MapE", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the end of Map.\n");
		return false;
	}
	
	return true;
}

void Map::save(SDL_RWops *stream)
{
	SDL_RWwrite(stream, "MapB", 4, 1);
	
	// We save size:
	SDL_WriteBE32(stream, wDec);
	SDL_WriteBE32(stream, hDec);

	// We write what's inside the map:
	SDL_RWwrite(stream, undermap, size, 1);
	for (int i=0; i<size ;i++)
	{
		SDL_WriteBE32(stream, mapDiscovered[i]);

		SDL_WriteBE16(stream, cases[i].terrain);
		SDL_WriteBE16(stream, cases[i].building);
		
		//SDL_WriteBE32(stream, cases[i].ressource.id);
		SDL_RWwrite(stream, &(cases[i].ressource), 1, 4);
		
		SDL_WriteBE16(stream, cases[i].groundUnit);
		SDL_WriteBE16(stream, cases[i].airUnit);
		SDL_WriteBE32(stream, cases[i].forbidden);
	}

	// We save sectors:
	SDL_WriteBE32(stream, wSector);
	SDL_WriteBE32(stream, hSector);
	for (int i=0; i<sizeSector; i++)
		sectors[i].save(stream);

	SDL_RWwrite(stream, "MapE", 4, 1);
}

void Map::addTeam(void)
{
	int numberOfTeam=game->session.numberOfTeam;
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
			updateGradient(t, r, s, true);
		}
}

void Map::removeTeam(void)
{
	int numberOfTeam=game->session.numberOfTeam;
	int oldNumberOfTeam=numberOfTeam+1;
	assert(numberOfTeam<32);
	
	for (int t=0; t<oldNumberOfTeam; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				assert(ressourcesGradient[t][r][s]);
	for (int t=oldNumberOfTeam; t<32; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				assert(ressourcesGradient[t][r][s]==NULL);
	
	int t=numberOfTeam;
	for (int r=0; r<MAX_RESSOURCES; r++)
		for (int s=0; s<2; s++)
		{
			assert(ressourcesGradient[t][r][s]);
			delete[] ressourcesGradient[t][r][s];
			ressourcesGradient[t][r][s]=NULL;
		}
}

// TODO: completely recreate:
void Map::growRessources(void)
{
	int dy=syncRand()&0x3;
	for (int y=dy; y<h; y+=4)
	{
		for (int x=(syncRand()&0xF); x<w; x+=(syncRand()&0x1F))
		//for (int x=0; x<w; x++)
		{
			//int y=syncRand()&hMask;
			Ressource r=getRessource(x, y);
			if (r.type!=NO_RES_TYPE)
			{
				// we look around to see if there is any water :
				// TODO: uses UnderMap.
				static const int waterDist=0xF;
				int dwax=(syncRand()&waterDist)-(syncRand()&waterDist);
				int dway=(syncRand()&waterDist)-(syncRand()&waterDist);
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
				if (r.type==ALGA)
					expand=isWater(wax1, way1)&&isSand(wax2, way2);
				else if (r.type==WOOD)
					expand=isWater(wax1, way1)&&(!isSand(wax3, way3));
				else if (r.type==CORN)
					expand=isWater(wax1, way1)&&(!isSand(wax3, way3));

				if (expand)
				{
					if (r.amount<=(int)(syncRand()&7))
					{
						// we grow ressource:
						incRessource(x, y, r.type, r.variety);
					}
					else if (globalContainer->ressourcesTypes.get(r.type)->expendable)
					{
						// we extand ressource:
						int dx, dy;
						Unit::dxdyfromDirection(syncRand()&7, &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						incRessource(nx, ny, r.type, r.variety);
					}
				}
			}
		}
	}
}

void Map::syncStep(Sint32 stepCounter)
{
	growRessources();
	for (int i=0; i<sizeSector; i++)
		sectors[i].step();
	
	// We only update one gradient per step:
	bool updated=false;
	while (!updated)
	{
		int numberOfTeam=game->session.numberOfTeam;
		for (int t=0; t<numberOfTeam; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
				{
					int gud=gradientUpdatedDepth[t][r][s];
					if (gud<2)
					{
						//printf("updateGradient(%d, %d, %d, %d)\n", t, r, s, gud==0);
						updateGradient(t, r, s, gud==0);
						gradientUpdatedDepth[t][r][s]++;
						updated=true;
						goto tripleBreak;
					}
				}
		tripleBreak:
		if (!updated)
		{
			for (int t=0; t<numberOfTeam; t++)
				for (int r=0; r<MAX_RESSOURCES; r++)
					for (int s=0; s<2; s++)
						gradientUpdatedDepth[t][r][s]=0;
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

void Map::setForbiddenArea(int x, int y, int r, Uint32 me)
{
	int rm=(r<<1)+1;
	int r2=rm*rm;
	for (int yi=-r; yi<=r; yi++)
	{
		int yi2=((yi*yi)<<2);
		for (int xi=-r; xi<=r; xi++)
			if (yi2+((xi*xi)<<2)<r2)
				(*(cases+w*((y+yi)&hMask)+((x+xi)&wMask))).forbidden|=me;
	}
}

void Map::clearForbiddenArea(int x, int y, int r, Uint32 me)
{
	int rm=(r<<1)+1;
	int r2=rm*rm;
	for (int yi=-r; yi<=r; yi++)
	{
		int yi2=((yi*yi)<<2);
		for (int xi=-r; xi<=r; xi++)
			if (yi2+((xi*xi)<<2)<r2)
				(*(cases+w*((y+yi)&hMask)+((x+xi)&wMask))).forbidden&=~me;
	}
}

void Map::clearForbiddenArea(Uint32 me)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			(*(cases+w*y+x)).forbidden&=~me;
}

void Map::decRessource(int x, int y)
{
	Ressource *rp=&(*(cases+w*(y&hMask)+(x&wMask))).ressource;
	Ressource r=*rp;

	if (r.type==NO_RES_TYPE)
		return;

	int type=r.type;
	const RessourceType *fulltype=globalContainer->ressourcesTypes.get(type);
	unsigned amount=r.amount;
	assert(amount);

	if (!fulltype->shrinkable)
		return;
	if (fulltype->eternal)
	{
		if (amount>0)
			rp->amount=amount-1;
	}
	else
	{
		if (!fulltype->granular || amount==1)
			rp->clear();
		else
			rp->amount=amount-1;
	}
}

void Map::decRessource(int x, int y, int ressourceType)
{
	if (isRessource(x, y, ressourceType))
		decRessource(x, y);
}

bool Map::incRessource(int x, int y, int ressourceType, int variety)
{
	Ressource *rp=&(*(cases+w*(y&hMask)+(x&wMask))).ressource;
	Ressource &r=*rp;
	const RessourceType *fulltype;
	if (r.type==NO_RES_TYPE)
	{
		if (getBuilding(x, y)!=NOGBID)
			return false;
		if (getGroundUnit(x, y)!=NOGUID)
			return false;

		fulltype=globalContainer->ressourcesTypes.get(ressourceType);
		if (getTerrainType(x, y) == fulltype->terrain)
		{
			rp->type=ressourceType;
			rp->variety=variety;
			rp->amount=1;
			rp->animation=0;
			return true;
		}
		else
			return false;
	}
	else
	{
		fulltype=globalContainer->ressourcesTypes.get(r.type);
	}

	if (r.type!=ressourceType)
		return false;
	if (!fulltype->shrinkable)
		return false;
	if (r.amount<fulltype->sizesCount)
	{
		rp->amount=r.amount+1;
		return true;
	}
	else
	{
		rp->amount=r.amount-1;
	}
	return false;
}

bool Map::isFreeForGroundUnit(int x, int y, bool canSwim, Uint32 teamMask)
{
	if (isRessource(x+w, y+h))
		return false;
	if (getBuilding(x+w, y+h)!=NOGBID)
		return false;
	if (getGroundUnit(x+w, y+h)!=NOGUID)
		return false;
	if (!canSwim && isWater(x+w, y+h))
		return false;
	if (getForbidden(x+w, y+h)&teamMask)
		return false;
	return true;
}

bool Map::isFreeForGroundUnitNoForbidden(int x, int y, bool canSwim)
{
	if (isRessource(x+w, y+h))
		return false;
	if (getBuilding(x+w, y+h)!=NOGBID)
		return false;
	if (getGroundUnit(x+w, y+h)!=NOGUID)
		return false;
	if (!canSwim && isWater(x+w, y+h))
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
	
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy))
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
	
	for (int tdx=-1; tdx<=1; tdx++)
		for (int tdy=-1; tdy<=1; tdy++)
			if (isRessource(x+tdx, y+tdy, ressourceType))
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
			if (isRessource(x+tdx, y+tdy,ressourceType))
			{
				*dx=tdx;
				*dy=tdy;
				return true;
			}
	return false;
}

//! This method gives a good direction to hit for a warrior, and return fase is nothing was found.
//! Currently, it chooses to hit any turret if aviable, then units, then other buildings.
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
					if ((otherUnit->foreingExchangeBuilding==NULL) || ((unit->owner->sharedVisionExchange & otherTeamMask)==0))
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

void Map::setUMatPos(int x, int y, TerrainType t, int size)
{
	for (int dx=x-(size>>1); dx<x+(size>>1)+1; dx++)
		for (int dy=y-(size>>1); dy<y+(size>>1)+1; dy++)
		{
			if (t==GRASS)
			{
				if (getUMTerrain(dx,dy-1)==WATER)
					setUMTerrain(dx,dy-1,SAND);
				if (getUMTerrain(dx,dy+1)==WATER)
					setUMTerrain(dx,dy+1,SAND);

				if (getUMTerrain(dx-1,dy)==WATER)
					setUMTerrain(dx-1,dy,SAND);
				if (getUMTerrain(dx+1,dy)==WATER)
					setUMTerrain(dx+1,dy,SAND);

				if (getUMTerrain(dx-1,dy-1)==WATER)
					setUMTerrain(dx-1,dy-1,SAND);
				if (getUMTerrain(dx+1,dy-1)==WATER)
					setUMTerrain(dx+1,dy-1,SAND);

				if (getUMTerrain(dx+1,dy+1)==WATER)
					setUMTerrain(dx+1,dy+1,SAND);
				if (getUMTerrain(dx-1,dy+1)==WATER)
					setUMTerrain(dx-1,dy+1,SAND);
			}
			else if (t==WATER)
			{
				if (getUMTerrain(dx,dy-1)==GRASS)
					setUMTerrain(dx,dy-1,SAND);
				if (getUMTerrain(dx,dy+1)==GRASS)
					setUMTerrain(dx,dy+1,SAND);

				if (getUMTerrain(dx-1,dy)==GRASS)
					setUMTerrain(dx-1,dy,SAND);
				if (getUMTerrain(dx+1,dy)==GRASS)
					setUMTerrain(dx+1,dy,SAND);

				if (getUMTerrain(dx-1,dy-1)==GRASS)
					setUMTerrain(dx-1,dy-1,SAND);
				if (getUMTerrain(dx+1,dy-1)==GRASS)
					setUMTerrain(dx+1,dy-1,SAND);

				if (getUMTerrain(dx+1,dy+1)==GRASS)
					setUMTerrain(dx+1,dy+1,SAND);
				if (getUMTerrain(dx-1,dy+1)==GRASS)
					setUMTerrain(dx-1,dy+1,SAND);
			}
			setUMTerrain(dx,dy,t);
		}
	if (t==SAND)
		regenerateMap(x-(size>>1)-1,y-(size>>1)-1,size+1,size+1);
	else
		regenerateMap(x-(size>>1)-2,y-(size>>1)-2,size+3,size+3);
}

void Map::setNoRessource(int x, int y, int size)
{
	assert(size>=0);
	assert(size<w);
	assert(size<h);
	for (int dx=x-(size>>1); dx<x+(size>>1); dx++)
		for (int dy=y-(size>>1); dy<y+(size>>1); dy++)
			(cases+w*(dy&hMask)+(dx&wMask))->ressource.clear();
}

void Map::setRessource(int x, int y, int type, int size)
{
	assert(size>=0);
	assert(size<w);
	assert(size<h);
	for (int dx=x-(size>>1); dx<x+(size>>1)+1; dx++)
		for (int dy=y-(size>>1); dy<y+(size>>1)+1; dy++)
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
	return (getTerrainType(x, y)==globalContainer->ressourcesTypes.get(type)->terrain);
}

void Map::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	int x=mx-viewportX;
	int y=my-viewportY;
	
	if (x>(w/2))
		x-=w;
	if (y>(h/2))
		y-=h;
	if ((x)<-(w/2))
		x+=w;
	if ((y)<-(h/2))
		y+=getH();
	*px=x<<5;
	*py=y<<5;
}

void Map::displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	*px=((mx>>5)+viewportX+getW())&getMaskW();
	*py=((my>>5)+viewportY+getH())&getMaskH();
}

void Map::displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY)
{
	*px=(((mx+16)>>5)+viewportX+getW())&getMaskW();
	*py=(((my+16)>>5)+viewportY+getH())&getMaskH();
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
		
	*px=(tempX+getW())&getMaskW();
	*py=(tempY+getH())&getMaskH();
}

void Map::buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY)
{
	mapCaseToDisplayable(px, py, mx, my, viewportX, viewportY);
	*mx+=buildingWidth*16;
	*my+=buildingHeight*16;
}

bool Map::ressourceAviable(int teamNumber, int ressourceType, bool canSwim, int x, int y)
{
	Uint8 g=getGradient(teamNumber, ressourceType, canSwim, x, y);
	return g>1; //Becasue 0==obstacle, 1==no obstacle, but you don't know if there is anything around.
}

bool Map::ressourceAviable(int teamNumber, int ressourceType, bool canSwim, int x, int y, int *dist)
{
	Uint8 g=getGradient(teamNumber, ressourceType, canSwim, x, y);
	if (g>1)
	{
		*dist=255-g;
		return true;
	}
	else
		return false;
}

bool Map::ressourceAviable(int teamNumber, int ressourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist)
{
	ressourceAviableCount[teamNumber][ressourceType]++;
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	x&=wMask;
	y&=hMask;
	Uint8 g=gradient[(y<<wDec)+x];
	if (g<2)
	{
		ressourceAviableCountFast[teamNumber][ressourceType]++;
		return false;
	}
	if (dist)
		*dist=255-g;
	if (g>=255)
	{
		ressourceAviableCountFast[teamNumber][ressourceType]++;
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
			static const int tab[8][2]={
				{ 0, -1},
				{ 1,  0},
				{ 0,  1},
				{-1,  0},
				{-1, -1},
				{ 1, -1},
				{ 1,  1},
				{-1,  1}};
			int ddx=tab[d][0];
			int ddy=tab[d][1];
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
			ressourceAviableCountFar[teamNumber][ressourceType]++;
			Uint8 miniGrad[25];
			for (int ry=0; ry<5; ry++)
				for (int rx=0; rx<5; rx++)
				{
					int xg=(vx+rx+w-2)&wMask;
					int yg=(vy+ry+h-2)&hMask;
					miniGrad[rx+ry*5]=gradient[xg+yg*w];
				}
					
			if (directionFromMinigrad(miniGrad, &vddx, &vddy, false))
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
			ressourceAviableCountSuccess[teamNumber][ressourceType]++;
			*targetX=vx;
			*targetY=vy;
			return true;
		}
		if (!found)
		{
			{
				int vx=x&wMask;
				int vy=y&hMask;
				printf("init v=(%d, %d)\n", vx, vy);

				Uint8 max=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
				for (int count=0; count<255; count++)
				{
					bool found=false;
					int vddx=0;
					int vddy=0;
					for (int d=0; d<8; d++)
					{
						static const int tab[8][2]={
							{ 0, -1},
							{ 1,  0},
							{ 0,  1},
							{-1,  0},
							{-1, -1},
							{ 1, -1},
							{ 1,  1},
							{-1,  1}};
						int ddx=tab[d][0];
						int ddy=tab[d][1];
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
						printf("fast v=(%d, %d), max=%d\n", vx, vy, max);
					}
					else
					{
						Uint8 miniGrad[25];
						for (int ry=0; ry<5; ry++)
							for (int rx=0; rx<5; rx++)
							{
								int xg=(vx+rx+w-2)&wMask;
								int yg=(vy+ry+h-2)&hMask;
								miniGrad[rx+ry*5]=gradient[xg+yg*w];
							}

						if (directionFromMinigrad(miniGrad, &vddx, &vddy, false))
						{
							vx=(vx+vddx)&wMask;
							vy=(vy+vddy)&hMask;
							Uint8 g=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
							found=true;
							if (g>max)
								max=g;
							printf("mini v=(%d, %d), g=%d, max=%d\n", vx, vy, g, max);
						}
					}
					if (max==255 || (max>=255 && (getBuilding(vx, vy)==NOGBID)))
					{
						printf("return true v=(%d, %d), max=%d\n", vx, vy, max);
						break;
					}
					if (!found)
					{
						printf("return false\n");
						break;
					}
				}
			}
			
			ressourceAviableCountFailureBase[teamNumber][ressourceType]++;
			fprintf(logFile, "target *not* found! pos=(%d, %d), vpos=(%d, %d), max=%d, team=%d, res=%d, swim=%d\n", x, y, vx, vy, max, teamNumber, ressourceType, canSwim);
			printf("target *not* found! pos=(%d, %d), vpos=(%d, %d), max=%d, team=%d, res=%d, swim=%d\n", x, y, vx, vy, max, teamNumber, ressourceType, canSwim);
			*targetX=vx;
			*targetY=vy;
			return false;
		}
	}
	
	{
		int vx=x&wMask;
		int vy=y&hMask;
		printf("init v=(%d, %d)\n", vx, vy);

		Uint8 max=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
		for (int count=0; count<255; count++)
		{
			bool found=false;
			int vddx=0;
			int vddy=0;
			for (int d=0; d<8; d++)
			{
				static const int tab[8][2]={
					{ 0, -1},
					{ 1,  0},
					{ 0,  1},
					{-1,  0},
					{-1, -1},
					{ 1, -1},
					{ 1,  1},
					{-1,  1}};
				int ddx=tab[d][0];
				int ddy=tab[d][1];
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
				printf("fast v=(%d, %d), max=%d\n", vx, vy, max);
			}
			else
			{
				Uint8 miniGrad[25];
				for (int ry=0; ry<5; ry++)
					for (int rx=0; rx<5; rx++)
					{
						int xg=(vx+rx+w-2)&wMask;
						int yg=(vy+ry+h-2)&hMask;
						miniGrad[rx+ry*5]=gradient[xg+yg*w];
					}

				if (directionFromMinigrad(miniGrad, &vddx, &vddy, false))
				{
					vx=(vx+vddx)&wMask;
					vy=(vy+vddy)&hMask;
					Uint8 g=gradient[(vx&wMask)+((vy&hMask)<<wDec)];
					found=true;
					if (g>max)
						max=g;
					printf("mini v=(%d, %d), g=%d, max=%d\n", vx, vy, g, max);
				}
			}
			if (max==255 || (max>=255 && (getBuilding(vx, vy)==NOGBID)))
			{
				printf("return true v=(%d, %d), max=%d\n", vx, vy, max);
				break;
			}
			if (!found)
			{
				printf("return false\n");
				break;
			}
		}
	}
	
	ressourceAviableCountFailureOvercount[teamNumber][ressourceType]++;
	fprintf(logFile, "target *not* found! (count>255) pos=(%d, %d), vpos=(%d, %d), team=%d, res=%d, swim=%d\n", x, y, vx, vy, teamNumber, ressourceType, canSwim);
	printf("target *not* found! (count>255) pos=(%d, %d), vpos=(%d, %d), team=%d, res=%d, swim=%d\n", x, y, vx, vy, teamNumber, ressourceType, canSwim);
	*targetX=vx;
	*targetY=vy;
	return false;
}

/*void Map::updateGlobalGradient(Uint8 *gradient)
{
	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	// Warning, this is *nearly* a copy-past, 4 times, once for each direction.
	
	//start(2/4, 3/4)
	for (int yi=(3*(h>>1)); yi<(h+3*(h>>1)); yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi+hMask)&hMask)<<wDec);
		for (int xi=(w>>1); xi<(w+(w>>1)); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x+wMask)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];

				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	//start(0/4, 2/4)
	for (int yi=(h+(h>>2)); yi>=(h>>2); yi--)
	{
		int wy=((yi&hMask)<<wDec);
		int wyd=(((yi+1)&hMask)<<wDec);
		for (int x=0; x<w; x++)
		{
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x+wMask)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	//start(3/4, 1/4)
	for (int xi=(3*(w>>2)); xi<(w+3*(w>>2)); xi++)
	{
		int x=(xi&wMask);
		int xl=(xi+wMask)&wMask;
		for (int yi=(h+(h>>2)); yi>=(h>>2); yi--)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi+hMask)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	//start(1/4, 0/4)
	for (int xi=(w+(w>>2)); xi>=(w>>2); xi--)
	{
		int x=(xi&wMask);
		int xr=(xi+1)&wMask;
		for (int y=0; y<h; y++)
		{
			int wy=(y<<wDec);
			int wyu=(((y+hMask)&hMask)<<wDec);
			int wyd=(((y+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}*/


void Map::updateGlobalGradient(Uint8 *gradient)
{
	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	// Warning, this is *nearly* a copy-past, 4 times, once for each direction.
	
	for (int yi=0; yi<h; yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi+hMask)&hMask)<<wDec);
		for (int xi=yi; xi<(yi+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x+wMask)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];

				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int y=hMask; y>=0; y--)
	{
		int wy=(y<<wDec);
		int wyd=(((y+1)&hMask)<<wDec);
		for (int xi=y; xi<(y+w); xi++)
		{
			int x=xi&wMask;
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x+wMask)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyd+xr];
				side[1]=gradient[wyd+x ];
				side[2]=gradient[wyd+xl];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=0; x<w; x++)
	{
		int xl=(x+wMask)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi+hMask)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyd+xl];
				side[2]=gradient[wy +xl];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}

	for (int x=wMask; x>=0; x--)
	{
		int xr=(x+1)&wMask;
		for (int yi=x; yi<(x+h); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi+hMask)&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				Uint8 side[4];
				side[0]=gradient[wyu+xr];
				side[1]=gradient[wy +xr];
				side[2]=gradient[wyd+xr];
				side[3]=gradient[wyu+x ];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];
				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}
}

void Map::updateGradient(int teamNumber, Uint8 ressourceType, bool canSwim, bool init)
{
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (init)
	{
		assert(globalContainer);
		bool visibleToBeCollected=globalContainer->ressourcesTypes.get(ressourceType)->visibleToBeCollected;
		memset(gradient, 1, size);
		for (int y=0; y<h; y++)
		{
			int wy=(y<<wDec);
			for (int x=0; x<w; x++)
			{
				Case c=cases[wy+x];
				if (c.ressource.type==NO_RES_TYPE)
				{
					if (c.building!=NOGBID)
						gradient[wy+x]=0;
					else if (c.forbidden&teamMask)
						gradient[wy+x]=0;
					else if (!canSwim && isWater(x, y))
						gradient[wy+x]=0;
				}
				else
				{
					if (c.ressource.type==ressourceType)
					{
						if (visibleToBeCollected && !(fogOfWar[wy+x]&teamMask))
							gradient[wy+x]=0;
						else
							gradient[wy+x]=255;
					}
					else
						gradient[wy+x]=0;
				}
			}
		}
	}
	
	for (int depth=0; depth<1; depth++) // With a higher depth, we can have more complex obstacles.
		updateGlobalGradient(gradient);
}

bool Map::directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict)
{
	Uint8 max;
	Uint8 maxs[8];
	
	max=miniGrad[1+1*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[5];
		side[0]=miniGrad[0+2*5];
		side[1]=miniGrad[0+1*5];
		side[2]=miniGrad[0+0*5];
		side[3]=miniGrad[1+0*5];
		side[4]=miniGrad[2+0*5];
		for (int i=0; i<5; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[0]=max;
	max=miniGrad[3+1*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[5];
		side[0]=miniGrad[2+0*5];
		side[1]=miniGrad[3+0*5];
		side[2]=miniGrad[4+0*5];
		side[3]=miniGrad[4+1*5];
		side[4]=miniGrad[4+2*5];
		for (int i=0; i<5; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[1]=max;
	max=miniGrad[3+3*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[5];
		side[0]=miniGrad[4+2*5];
		side[1]=miniGrad[4+3*5];
		side[2]=miniGrad[4+4*5];
		side[3]=miniGrad[3+4*5];
		side[4]=miniGrad[2+4*5];
		for (int i=0; i<5; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[2]=max;
	max=miniGrad[1+3*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[5];
		side[0]=miniGrad[2+4*5];
		side[1]=miniGrad[1+4*5];
		side[2]=miniGrad[0+4*5];
		side[3]=miniGrad[0+3*5];
		side[4]=miniGrad[0+2*5];
		for (int i=0; i<5; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[3]=max;
	
	
	max=miniGrad[2+1*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[3];
		side[0]=miniGrad[1+0*5];
		side[1]=miniGrad[2+0*5];
		side[2]=miniGrad[3+0*5];
		for (int i=0; i<3; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[4]=max;
	max=miniGrad[3+2*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[3];
		side[0]=miniGrad[4+1*5];
		side[1]=miniGrad[4+2*5];
		side[2]=miniGrad[4+3*5];
		for (int i=0; i<3; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[5]=max;
	max=miniGrad[2+3*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[3];
		side[0]=miniGrad[1+4*5];
		side[1]=miniGrad[2+4*5];
		side[2]=miniGrad[3+4*5];
		for (int i=0; i<3; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[6]=max;
	max=miniGrad[1+2*5];
	if (max && max!=255)
	{
		max=1;
		Uint8 side[3];
		side[0]=miniGrad[0+1*5];
		side[1]=miniGrad[0+2*5];
		side[2]=miniGrad[0+3*5];
		for (int i=0; i<3; i++)
			if (side[i]>max)
				max=side[i];
	}
	maxs[7]=max;
	
	int centerg=miniGrad[2+2*5];
	int maxg=centerg;
	int maxd=8;
	bool good=false;
	if (strict)
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
	else
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
	
	/*printf("miniGrad:\n");
	for (int ry=0; ry<5; ry++)
	{
		for (int rx=0; rx<5; rx++)
			printf("%4d", miniGrad[rx+ry*5]);
		printf("\n");
	}
	printf("maxs:\n");
	for (int d=0; d<8; d++)
		printf("%4d\n", maxs[d]);
	printf("maxd=%4d\n", maxd);*/
	
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

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, Uint8 *gradient)
{
	Uint8 miniGrad[25];
	miniGrad[2+2*5]=gradient[x+y*w];
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
	static const int tabClose[8][2]={
		{-1, -1},
		{ 0, -1},
		{ 1, -1},
		{ 1,  0},
		{ 1,  1},
		{ 0,  1},
		{-1,  1},
		{-1,  0}};
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
	return directionFromMinigrad(miniGrad, dx, dy, true);
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024])
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
				gx=(gx+wMask)&wMask;
				lx=31;
			}
			if (ly==hMask)
			{
				gy=(gy+1)&hMask;
				ly=0;
			}
			else if (ly==32)
			{
				gy=(gy+hMask)&hMask;
				ly=31;
			}
			assert(lx>=0);
			assert(ly>=0);
			assert(lx<32);
			assert(ly<32);
			int g=localGradient[lx+ly*32];
			//printf("|r=(%d, %d), b=(%d, %d), g=(%d, %d), l=(%d, %d), g=%d\n", rx, ry, bx, by, gx, gy, lx, ly, g);
			if (g==0 || g==255 || isFreeForGroundUnit(gx, gy, canSwim, teamMask))
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
	return directionFromMinigrad(miniGrad, dx, dy, true);
}

bool Map::pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork)
{
	pathToRessourceCountTot++;
	//printf("pathfindingRessource...\n");
	assert(ressourceType<MAX_RESSOURCES);
	Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	Uint8 max=gradient[x+y*w];
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (max<2)
	{
		pathToRessourceCountFailure++;
		*stopWork=true;
		return false;
	}
	
	if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient))
	{
		pathToRessourceCountSuccess++;
		//printf("...pathfindedRessource v2 %d\n", found);
		return true;
	}
	else
	{
		pathToRessourceCountFailure++;
		printf("locked at (%d, %d) for r=%d, max=%d\n", x, y, ressourceType, max);
		fprintf(logFile, "locked at (%d, %d) for r=%d, max=%d\n", x, y, ressourceType, max);
		*stopWork=false;
		return false;
	}
}

void Map::updateLocalGradient(Building *building, bool canSwim)
{
	localBuildingGradientUpdate++;
	//fprintf(logFile, "updatingLocalGradient (gbid=%d)...\n", building->gid);
	//printf("updatingLocalGradient (gbid=%d)...\n", building->gid);
	assert(building);
	assert(building->type);
	building->dirtyLocalGradient[canSwim]=false;
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	int posH=building->type->height;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Uint8 *gradient=building->localGradient[canSwim];

	memset(gradient, 1, 1024);
	if (building->type->isVirtual)
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=yi*yi;
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<r2)
				{
					int xxi=15+xi;
					int yyi=15+yi;
					if (xxi<0)
						xxi=0;
					else if (xxi>31)
						xxi=31;
					if (yyi<0)
						yyi=0;
					else if (yyi>31)
						xxi=31;
					gradient[xxi+(yyi<<5)]=255;
				}
		}
	}
	else
		for (int dy=0; dy<posH; dy++)
			for (int dx=0; dx<posW; dx++)
			{
					int xxi=15+dx;
					int yyi=15+dy;
					if (xxi<0)
						xxi=0;
					else if (xxi>31)
						xxi=31;
					if (yyi<0)
						yyi=0;
					else if (yyi>31)
						xxi=31;
					gradient[xxi+(yyi<<5)]=255;
				}

	// Here g=Global(map axis), l=Local(map axis)

	for (int yl=0; yl<32; yl++)
	{
		int wyl=(yl<<5);
		int yg=(yl+posY-15+h)&hMask;
		int wyg=w*yg;
		for (int xl=0; xl<32; xl++)
		{
			int xg=(xl+posX-15+w)&wMask;
			Case c=cases[wyg+xg];
			int addrl=wyl+xl;
			if (gradient[addrl]!=255)
			{
				if (c.ressource.type!=NO_RES_TYPE)
					gradient[addrl]=0;
				else if (c.building!=NOGBID && c.building!=bgid)
					gradient[addrl]=0;
				else if (c.forbidden&teamMask)
					gradient[addrl]=0;
				else if (!canSwim && isWater(xg, yg))
					gradient[addrl]=0;
			}
		}
	}
	
	if (building->type->zonable[WORKER])
	{
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=yi*yi;
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<=r2)
				{
					int xxi=15+xi;
					int yyi=15+yi;
					if (xxi<0)
						xxi=0;
					else if (xxi>31)
						xxi=31;
					if (yyi<0)
						yyi=0;
					else if (yyi>31)
						xxi=31;
					gradient[xxi+(yyi<<5)]=255;
				}
			}
		}
	}
	
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
		return;
		doubleBreak:;
	}
	else
		building->locked[canSwim]=false;

	//In this algotithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.

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

								Uint8 side[8];
								side[0]=gradient[wyu+xl];
								side[1]=gradient[wyu+x ];
								side[2]=gradient[wyu+xr];

								side[3]=gradient[wy +xr];

								side[4]=gradient[wyd+xr];
								side[5]=gradient[wyd+x ];
								side[6]=gradient[wyd+xl];

								side[7]=gradient[wy +xl];

								for (int i=0; i<8; i++)
									if (side[i]>max)
										max=side[i];
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
	globalBuildingGradientUpdate++;
	assert(building);
	assert(building->type);
	//printf("updatingGlobalGradient (gbid=%d)\n", building->gid);
	//fprintf(logFile, "updatingGlobalGradient (gbid=%d)...", building->gid);
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	int posH=building->type->height;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Uint8 *gradient=building->globalGradient[canSwim];
	assert(gradient);

	memset(gradient, 1, size);
	if (building->type->isVirtual)
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<r2)
					gradient[((posX+w+xi)&wMask)+(w*((posY+h+yi)&hMask))]=255;
		}
	}
	else
	{
		for (int dy=0; dy<posH; dy++)
			for (int dx=0; dx<posW; dx++)
				gradient[((posX+dx)&wMask)+((posY+dy)&hMask)*w]=255;
	}

	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			Case c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.ressource.type!=NO_RES_TYPE)
					gradient[wyx]=0;
				else if (c.forbidden&teamMask)
					gradient[wyx]=0;
				else if (!canSwim && isWater(x, y))
					gradient[wyx]=0;
			}
			else
			{
				if (c.building==bgid)
					gradient[wyx]=255;
				else
					gradient[wyx]=0;
			}
		}
	}
	
	if (building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
					gradient[((posX+w+xi)&wMask)+(w*((posY+h+yi)&hMask))]=255;
		}
	}
	
	if (!building->type->isVirtual)
	{
		building->locked[canSwim]=true;
		int x=(posX+wMask)&wMask;
		int y=(posY+hMask)&hMask;
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
		return;
		doubleBreak:;
	}
	else
		building->locked[canSwim]=false;

	for (int depth=0; depth<1; depth++) // With a higher depth, we can have more complex obstacles.
	{
		int x=(posX+wMask)&wMask;
		int y=(posY+hMask)&hMask;
		
		for (int di=posW+1; di<=64; di+=2) //distance-iterator
		{
			for (int bi=0; bi<2; bi++) //back-iterator
				for (int ai=0; ai<4; ai++) //angle-iterator
				{
					for (int mi=0; mi<di; mi++) //move-iterator
					{
						//printf("di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
						assert(x>=0);
						assert(y>=0);
						assert(x<w);
						assert(y<h);

						int wy =w*((y)&hMask);
						int wyu=w*((y+hMask)&hMask);
						int wyd=w*((y+1)&hMask);
						
						Uint8 max=gradient[wy+x];
						if (max && max!=255)
						{
							int xl=(x+wMask)&wMask;
							int xr=(x+1)&wMask;

							Uint8 side[8];
							side[0]=gradient[wyu+xl];
							side[1]=gradient[wyu+x ];
							side[2]=gradient[wyu+xr];

							side[3]=gradient[wy +xr];

							side[4]=gradient[wyd+xr];
							side[5]=gradient[wyd+x ];
							side[6]=gradient[wyd+xl];

							side[7]=gradient[wy +xl];

							for (int i=0; i<8; i++)
								if (side[i]>max)
									max=side[i];
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
									x=(x+1)&wMask;
								break;
								case 1:
									y=(y+1)&hMask;
								break;
								case 2:
									x=(x+wMask)&wMask;
								break;
								case 3:
									y=(y+hMask)&hMask;
								break;
							}
						}
						else
						{
							switch (ai)
							{
								case 0:
									y=(y+1)&hMask;
								break;
								case 1:
									x=(x+1)&wMask;
								break;
								case 2:
									y=(y+hMask)&hMask;
								break;
								case 3:
									x=(x+wMask)&wMask;
								break;
							}
						}
					}
				}
			x=(x+wMask)&wMask;
			y=(y+hMask)&hMask;
		}
	}
	
	updateGlobalGradient(gradient);
	
	/*for (int depth=0; depth<1; depth++) // With a higher depth, we can have more complex obstacles.
	{
	
		for (int yi=(h>>1); yi<(h+(h>>1)); yi++)
		{
			int wy=((yi&hMask)<<wDec);
			int wyu=(((yi+hMask)&hMask)<<wDec);
			for (int x=0; x<w; x++)
			{
				Uint8 max=gradient[wy+x];
				if (max && max!=255)
				{
					int xl=(x+wMask)&wMask;
					int xr=(x+1)&wMask;
					
					Uint8 side[4];
					side[0]=gradient[wyu+xl];
					side[1]=gradient[wyu+x ];
					side[2]=gradient[wyu+xr];
					side[3]=gradient[wy +xl];

					for (int i=0; i<4; i++)
						if (side[i]>max)
							max=side[i];
					
					if (max==1)
						gradient[wy+x]=1;
					else
						gradient[wy+x]=max-1;
				}
			}
		}

		for (int yi=(h+(h>>1)); yi>=(h>>1); yi--)
		{
			int wy=((yi&hMask)<<wDec);
			int wyd=(((yi+1)&hMask)<<wDec);
			for (int x=0; x<w; x++)
			{
				Uint8 max=gradient[wy+x];
				if (max && max!=255)
				{
					int xl=(x+wMask)&wMask;
					int xr=(x+1)&wMask;
					
					Uint8 side[4];
					side[0]=gradient[wyd+xr];
					side[1]=gradient[wyd+x ];
					side[2]=gradient[wyd+xl];
					side[3]=gradient[wy +xl];

					for (int i=0; i<4; i++)
						if (side[i]>max)
							max=side[i];
					if (max==1)
						gradient[wy+x]=1;
					else
						gradient[wy+x]=max-1;
				}
			}
		}

		for (int xi=(w>>1); xi<(w+(w>>1)); xi++)
		{
			int x=(xi&wMask);
			int xl=(xi+wMask)&wMask;
			for (int y=0; y<h; y++)
			{
				int wy=(y<<wDec);
				int wyu=(((y+hMask)&hMask)<<wDec);
				int wyd=(((y+1)&hMask)<<wDec);
				Uint8 max=gradient[wy+x];
				if (max && max!=255)
				{
					Uint8 side[4];
					side[0]=gradient[wyu+xl];
					side[1]=gradient[wyd+xl];
					side[2]=gradient[wy +xl];
					side[3]=gradient[wyu+x ];

					for (int i=0; i<4; i++)
						if (side[i]>max)
							max=side[i];
					if (max==1)
						gradient[wy+x]=1;
					else
						gradient[wy+x]=max-1;
				}
			}
		}

		for (int xi=(w+(w>>1)); xi>=(w>>1); xi--)
		{
			int x=(xi&wMask);
			int xr=(xi+1)&wMask;
			for (int y=0; y<h; y++)
			{
				int wy=(y<<wDec);
				int wyu=(((y+hMask)&hMask)<<wDec);
				int wyd=(((y+1)&hMask)<<wDec);
				Uint8 max=gradient[wy+x];
				if (max && max!=255)
				{
					Uint8 side[4];
					side[0]=gradient[wyu+xr];
					side[1]=gradient[wy +xr];
					side[2]=gradient[wyd+xr];
					side[3]=gradient[wyu+x ];

					for (int i=0; i<4; i++)
						if (side[i]>max)
							max=side[i];
					if (max==1)
						gradient[wy+x]=1;
					else
						gradient[wy+x]=max-1;
				}
			}
		}
	}*/
	
	/*for (int yi=(h>>1); yi<(h+(h>>1)); yi++)
	{
		int wy=((yi&hMask)<<wDec);
		int wyu=(((yi+hMask)&hMask)<<wDec);
		for (int x=0; x<w; x++)
		{
			Uint8 max=gradient[wy+x];
			if (max && max!=255)
			{
				int xl=(x+wMask)&wMask;
				int xr=(x+1)&wMask;

				Uint8 side[4];
				side[0]=gradient[wyu+xl];
				side[1]=gradient[wyu+x ];
				side[2]=gradient[wyu+xr];
				side[3]=gradient[wy +xl];

				for (int i=0; i<4; i++)
					if (side[i]>max)
						max=side[i];

				if (max==1)
					gradient[wy+x]=1;
				else
					gradient[wy+x]=max-1;
			}
		}
	}*/
	
	//fprintf(logFile, "...updatedGlobalGradient\n");
}

void Map::updateLocalRessources(Building *building, bool canSwim)
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
	if (!gradient)
	{
		gradient=new Uint8[1024];
		building->localRessources[canSwim]=gradient;
	}
	assert(gradient);
	
	bool *clearingRessources=building->clearingRessources;
	
	memset(gradient, 1, 1024);
	int range=building->unitStayRange;
	assert(range<=15);
	if (range>15)
		range=15;
	int range2=range*range;
	for (int yl=0; yl<32; yl++)
	{
		int wyl=(yl<<5);
		int yg=(yl+posY-15+h)&hMask;
		int wyg=w*yg;
		int dyl2=(yl-15)*(yl-15);
		for (int xl=0; xl<32; xl++)
		{
			int xg=(xl+posX-15+w)&wMask;
			Case c=cases[wyg+xg];
			int addrl=wyl+xl;
			int dist2=(xl-15)*(xl-15)+dyl2;
			if (dist2<=range2)
			{
				if (c.ressource.type!=NO_RES_TYPE)
				{
					Sint8 t=c.ressource.type;
					if (t<BASIC_COUNT && clearingRessources[t])
						gradient[addrl]=255;
					else
						gradient[addrl]=0;
				}
				else if (c.building!=NOGBID)
					gradient[addrl]=0;
				else if (c.forbidden&teamMask)
					gradient[addrl]=0;
				else if (!canSwim && isWater(xg, yg))
					gradient[addrl]=0;
			}
			else
				gradient[addrl]=0;
		}
	}
	expandLocalGradient(gradient);
	building->localRessourcesCleanTime=0;
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

								Uint8 side[8];
								side[0]=gradient[wyu+xl];
								side[1]=gradient[wyu+x ];
								side[2]=gradient[wyu+xr];

								side[3]=gradient[wy +xr];

								side[4]=gradient[wyd+xr];
								side[5]=gradient[wyd+x ];
								side[6]=gradient[wyd+xl];

								side[7]=gradient[wy +xl];

								for (int i=0; i<8; i++)
									if (side[i]>max)
										max=side[i];
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

bool Map::buildingAviable(Building *building, bool canSwim, int x, int y, int *dist)
{
	buildingAviableCountTot++;
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
		buildingAviableCountClose++;
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;
		if (!building->dirtyLocalGradient[canSwim])
		{
			Uint8 currentg=gradient[lx+(ly<<5)];
			if (currentg>1)
			{
				buildingAviableCountCloseSuccess++;
				*dist=255-currentg;
				return true;
			}
			else
				for (int d=0; d<8; d++)
				{
					int ddx, ddy;
					Unit::dxdyfromDirection(d, &ddx, &ddy);
					int lxddx=lx+ddx;
					if (lxddx<0)
						lxddx=0;
					else if(lxddx>31)
						lxddx=31;
					int lyddy=ly+ddy;
					if (lyddy<0)
						lyddy=0;
					else if(lyddy>31)
						lyddy=31;
					Uint8 g=gradient[lxddx+(lyddy<<5)];
					if (g>1)
					{
						buildingAviableCountCloseSuccessAround++;
						*dist=255-g;
						return true;
					}
				}
		}
		
		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
		{
			buildingAviableCountCloseFailureLocked++;
			//printf("ba-a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			
			return false;
		}
		
		Uint8 currentg=gradient[lx+ly*32];
		
		if (currentg>1)
		{
			buildingAviableCountCloseSuccessUpdate++;
			*dist=255-currentg;
			return true;
		}
		else
			for (int d=0; d<8; d++)
			{
				int ddx, ddy;
				Unit::dxdyfromDirection(d, &ddx, &ddy);
				int lxddx=lx+ddx;
				if (lxddx<0)
					lxddx=0;
				else if(lxddx>31)
					lxddx=31;
				int lyddy=ly+ddy;
				if (lyddy<0)
					lyddy=0;
				else if(lyddy>31)
					lyddy=31;
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (g>1)
				{
					buildingAviableCountCloseSuccessUpdateAround++;
					*dist=255-g;
					return true;
				}
			}
		buildingAviableCountCloseFailureEnd++;
	}
	else
		buildingAviableCountIsFar++;
	buildingAviableCountFar++;
	
	gradient=building->globalGradient[canSwim];
	if (gradient==NULL)
	{
		buildingAviableCountFarNew++;
		gradient=new Uint8[size];
		fprintf(logFile, "ba- allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
		building->globalGradient[canSwim]=gradient;
	}
	else
	{
		buildingAviableCountFarOld++;
		if (building->locked[canSwim])
		{
			buildingAviableCountFarOldFailureLocked++;
			//printf("ba-b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
		if (currentg>1)
		{
			buildingAviableCountFarOldSuccess++;
			*dist=255-currentg;
			return true;
		}
		else
		{
			for (int d=0; d<8; d++)
			{
				int ddx, ddy;
				Unit::dxdyfromDirection(d, &ddx, &ddy);
				int xddx=(x+ddx+w)&wMask;
				int yddy=(y+ddy+h)&hMask;
				Uint8 g=gradient[xddx+yddy*w];
				if (g>1)
				{
					buildingAviableCountFarOldSuccessAround++;
					*dist=255-g;
					return true;
				}
			}
			buildingAviableCountFarOldFailureEnd++;
			//printf("ba-c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
	}
	
	updateGlobalGradient(building, canSwim);
	if (building->locked[canSwim])
	{
		buildingAviableCountFarNewFailureLocked++;
		//printf("ba-d- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		fprintf(logFile, "ba-d- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		return false;
	}
	
	Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
	if (currentg>1)
	{
		buildingAviableCountFarNewSuccess++;
		*dist=255-currentg;
		return true;
	}
	else
	{
		for (int d=0; d<8; d++)
		{
			int ddx, ddy;
			Unit::dxdyfromDirection(d, &ddx, &ddy);
			int xddx=(x+ddx+w)&wMask;
			int yddy=(y+ddy+h)&hMask;
			Uint8 g=gradient[xddx+yddy*w];
			if (g>1)
			{
				buildingAviableCountFarNewSuccessClosely++;
				*dist=255-g;
				return true;
			}
		}
		if (building->type->isVirtual)
		{
			//printf("ba-e- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			//fprintf(logFile, "ba-e- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			buildingAviableCountFarNewFailureVirtual++;
		}
		else
		{
			printf("ba-f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "ba-f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			buildingAviableCountFarNewFailureEnd++;
		}
		return false;
	}
}

bool Map::pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy)
{
	pathToBuildingCountTot++;
	//printf("pathfindingBuilding (gbid=%d)...\n", building->gid);
	assert(building);
	int bx=building->posX;
	int by=building->posY;
	assert(x>=0);
	assert(y>=0);
	 
	Uint32 teamMask=building->owner->me;
	
	Uint8 * gradient;
	
	gradient=building->localGradient[canSwim];
	
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
			//printf("...pathfindedBuilding v1\n");
			return true;
		}

		if (!building->dirtyLocalGradient[canSwim] && currentg>1)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient))
			{
				pathToBuildingCountCloseSuccessBase++;
				//printf("...pathfindedBuilding v2\n");
				return true;
			}
		}

		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
		{
			pathToBuildingCountCloseFailureLocked++;
			//printf("a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "a- local gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}

		max=0;
		currentg=gradient[lx+ly*32];
		found=false;
		gradientUsable=false;
		if (currentg>1)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient))
			{
				pathToBuildingCountCloseSuccessUpdated++;
				//printf("...pathfindedBuilding v4\n");
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
		gradient=new Uint8[size];
		//printf("allocating globalGradient for gbid=%d (%p)\n", building->gid, gradient);
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
			//printf("b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "b- global gradient to building bgid=%d@(%d, %d) failed, locked. p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		else if (currentg==1)
		{
			pathToBuildingCountFarOldFailureBad++;
			//printf("c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			fprintf(logFile, "c- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
		else
			found=directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient);

		//printf("found=%d, d=(%d, %d)\n", found, *dx, *dy);
		if (found)
		{
			pathToBuildingCountFarOldSuccess++;
			//printf("...pathfindedBuilding v6\n");
			return true;
		}
		else
			pathToBuildingCountFarOldFailureUnusable++;
	}
	
	updateGlobalGradient(building, canSwim);
	if (building->locked[canSwim])
	{
		pathToBuildingCountFarUpdateFailureLocked++;
		//printf("e- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		fprintf(logFile, "e- global gradient to building bgid=%d@(%d, %d) failed, locked.\n", building->gid, building->posX, building->posY);
		return false;
	}
	
	Uint8 currentg=gradient[(x&wMask)+w*(y&hMask)];
	if (currentg>1)
	{
		if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient))
		{
			pathToBuildingCountFarUpdateSuccess++;
			//printf("...pathfindedBuilding v7\n");
			return true;
		}
	}
	
	if (building->type->isVirtual)
	{
		pathToBuildingCountFarUpdateFailureVirtual++;
		//printf("f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		//fprintf(logFile, "f- global gradient to building bgid=%d@(%d, %d) failed! p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
	}
	else
	{
		pathToBuildingCountFarUpdateFailureBad++;
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
	assert(gradient);
	assert(isInLocalGradient(x, y, bx, by));
	
	int lx=(x-bx+15+32)&31;
	int ly=(y-by+15+32)&31;
	int max=0;
	Uint8 currentg=gradient[lx+(ly<<5)];
	bool found=false;
	bool gradientUsable=false;
	
	if (currentg==1 && building->localRessourcesCleanTime++<8) 
	{
		// We wait a bit and avoid immediate recalculation, because there is probably no ressources.
		//printf("...pathfindedLocalRessource v0 failure waiting\n");
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
				int lxddx=lx+ddx;
				if (lxddx<0)
					lxddx=0;
				else if(lxddx>31)
					lxddx=31;
				int lyddy=ly+ddy;
				if (lyddy<0)
					lyddy=0;
				else if(lyddy>31)
					lyddy=31;
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+w+ddx, y+h+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+w+ddx, y+h+ddy, canSwim, teamMask))
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
				int lxddx=lx+ddx;
				if (lxddx<0)
					lxddx=0;
				else if(lxddx>31)
					lxddx=31;
				int lyddy=ly+ddy;
				if (lyddy<0)
					lyddy=0;
				else if(lyddy>31)
					lyddy=31;
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+w+ddx, y+h+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+w+ddx, y+h+ddy, canSwim, teamMask))
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
				printf("...pathfindedLocalRessource v4 locked\n");
				return true;
			}
		}
		else
		{
			pathfindLocalRessourceCountFailureUnusable++;
			fprintf(logFile, "lr-a- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			printf("lr-a- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
			return false;
		}
	}
	else
	{
		pathfindLocalRessourceCountFailureBad++;
		fprintf(logFile, "lr-b- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		printf("lr-b- failed to pathfind localRessource bgid=%d@(%d, %d) p=(%d, %d)\n", building->gid, building->posX, building->posY, x, y);
		return false;
	}
}

void Map::dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber)
{
	fprintf(logFile, "Map::dirtyLocalGradient(%d, %d, %d, %d, %d)\n", x, y, wl, hl, teamNumber);
	for (int hi=0; hi<hl; hi++)
	{
		int wyi=w*((y+h+hi)&hMask);
		for (int wi=0; wi<wl; wi++)
		{
			int xi=(x+w+wi)&wMask;
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

Sint32 Map::checkSum(bool heavy)
{
	Sint32 cs=size;
	if (heavy)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				cs+=(cases+w*(y&hMask)+(x&wMask))->terrain;
				cs+=(cases+w*(y&hMask)+(x&wMask))->building;
				cs+=(cases+w*(y&hMask)+(x&wMask))->ressource.getUint32();
				cs+=(cases+w*(y&hMask)+(x&wMask))->groundUnit;
				cs+=(cases+w*(y&hMask)+(x&wMask))->airUnit;
				cs=(cs<<1)|(cs>>31);
			}
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
