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
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


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


