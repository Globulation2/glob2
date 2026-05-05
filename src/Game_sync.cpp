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

#include <iostream>
#include <fstream>

#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <set>
#include <string>
#include <functional>
#include <algorithm>
#include <sstream>
#include <cmath>

#include <FileManager.h>
#include <GraphicContext.h>

#include "building_type.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Order.h"
#include "Unit.h"
#include "UnitSkin.h"
#include "Integrity.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"

#include "MapEdit.h"

#include "Brush.h"
#include "DynamicClouds.h"
#include "Bullet.h"
#include "TextStream.h"
#include "FertilityCalculatorDialog.h"

#include "NetMessage.h"

#include "ReplayWriter.h"

#define BULLET_IMGID 0

// Per-tick sync. Split out of Game.cpp.

void Game::buildProjectSyncStep(Sint32 localTeam)
{
	for (std::list<BuildProject>::iterator bpi=buildProjects.begin(); bpi!=buildProjects.end();)
	{
		int posX=bpi->posX&map.getMaskW();
		int posY=bpi->posY&map.getMaskH();
		int teamNumber=bpi->teamNumber;
		assert(teamNumber <= teamsCount());
		Sint32 typeNum=(bpi->typeNum);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
		int w=bt->width;
		int h=bt->height;
		if (!map.isHardSpaceForBuilding(posX, posY, w, h))
		{
			fprintf(logFile, "BuildProject failure (%d, %d)\n", posX, posY);
			Uint32 notTeamMask=~Team::teamNumberToMask(teamNumber);
			for (int y=posY; y<posY+h; y++)
				for (int x=posX; x<posX+w; x++)
				{
					size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].forbidden&=notTeamMask;
					// Update local map
					if (teamNumber == localTeam)
						map.localForbiddenMap.set(index, false);
				}
			map.updateForbiddenGradient(teamNumber);
			std::list<BuildProject>::iterator to_erase=bpi;
			bpi++;
			buildProjects.erase(to_erase);
			continue;
		}
		else if (checkRoomForBuilding(posX, posY, bt, teamNumber))
		{
			Building *b=addBuilding(posX, posY, typeNum, teamNumber, bpi->unitWorking, bpi->unitWorkingFuture);
			if (b)
			{
				Uint32 notTeamMask=~Team::teamNumberToMask(teamNumber);
				for (int y=posY; y<posY+h; y++)
					for (int x=posX; x<posX+w; x++)
					{
						size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
						// Update real map
						map.cases[index].forbidden&=notTeamMask;
						// Update local map
						if (teamNumber == localTeam)
							map.localForbiddenMap.set(index, false);
					}
				map.updateForbiddenGradient(teamNumber);
				b->owner->addToStaticAbilitiesLists(b);
				b->update();
				fprintf(logFile, "BuildProject success (%d, %d)\n", posX, posY);
				std::list<BuildProject>::iterator to_erase=bpi;
				bpi++;
				buildProjects.erase(to_erase);
				continue;
			}
		}
		bpi++;
	}
}

void Game::wonSyncStep(void)
{
	//TODO: sideeffects? 
	//std::list<std::shared_ptr<WinningCondition> >& conditions = 
	gameHeader.getWinningConditions();

	bool areAllDecided=true;
	//We do this twice, because some win conditions depend on other win conditions
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->checkWinConditions();
	}
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->checkWinConditions();
		if(teams[i]->winCondition == WCUnknown)
			areAllDecided=false;
	}
	isGameEnded = areAllDecided;

}

void Game::scriptSyncStep()
{
	// do a script step
	sgslScript.syncStep(gui);
	mapscript.syncStep(gui);
}



void Game::prestigeSyncStep()
{
	totalPrestige=0;
	totalPrestigeReached=false;
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		totalPrestige += teams[i]->prestige;
	}
	if(totalPrestige >= prestigeToReach)
	{
		totalPrestigeReached=true;
	}
}



void Game::syncStep(Sint32 localTeam)
{
	if (!anyPlayerWaited)
	{
		if (globalContainer->replayWriter && globalContainer->replayWriter->isValid())
		{
			globalContainer->replayWriter->advanceStep();
		}

		Uint64 startTick=SDL_GetTicks64();

		for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
			teams[i]->syncStep();

		map.syncStep(stepCounter);

		syncRand();

		if ((stepCounter&31)==16)
		{
			map.switchFogOfWar();
			for (int t=0; t<mapHeader.getNumberOfTeams(); t++)
				for (int i=0; i<Building::MAX_COUNT; i++)
				{
					Building *b=teams[t]->myBuildings[i];
					if (b)
					{
						assert(b->owner==teams[t]);
						assert(b->type);
					}
					if ((b)&&(!b->type->isBuildingSite || (b->type->level>0))&&(!b->type->isVirtual))
					{
						b->setMapDiscovered();
					}
				}
		}

		if ((stepCounter&15)==1)
			buildProjectSyncStep(localTeam);

		if ((stepCounter&31)==0)
		{
			prestigeSyncStep();
			scriptSyncStep();
			wonSyncStep();
		}

		Uint64 endTick=SDL_GetTicks64();
		ticksGameSum[stepCounter&31]+=static_cast<Sint64>(endTick) - static_cast<Sint64>(startTick);
		stepCounter++;
		anyPlayerWaitedTimeFor+=1;
	}
}

void Game::dirtyWarFlagGradient(void)
{
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
		teams[i]->dirtyWarFlagGradient();
}
