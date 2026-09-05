// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière


#include "AICastor.h"

#include <assert.h>
#include <string.h>



#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "SDLCompat.h"


#include "Brush.h"
#include "FertilityCalculatorDialog.h"

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
			Uint32 notTeamMask=~Team::teamNumberToMask(teamNumber);
			for (int y=posY; y<posY+h; y++)
				for (int x=posX; x<posX+w; x++)
				{
					size_t index=(x&map.wMask)+(((y&map.hMask)<<map.wDec));
					// Update real map
					map.cases[index].forbidden&=notTeamMask;
					// Update local map
					if (teamNumber == localTeam)
						map.displayedForbiddenView.set(index, false);
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
							map.displayedForbiddenView.set(index, false);
					}
				map.updateForbiddenGradient(teamNumber);
				b->owner->addToStaticAbilitiesLists(b);
				b->update();
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

		if ((stepCounter&FOW_SWITCH_TICK_MASK)==FOW_SWITCH_TICK_PHASE)
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

		if ((stepCounter&BUILD_PROJECT_TICK_MASK)==BUILD_PROJECT_TICK_PHASE)
			buildProjectSyncStep(localTeam);

		if ((stepCounter&WORLD_LOGIC_TICK_MASK)==WORLD_LOGIC_TICK_PHASE)
		{
			prestigeSyncStep();
			scriptSyncStep();
			wonSyncStep();
		}

		Uint64 endTick=SDL_GetTicks64();
		ticksGameSum[stepCounter&(TICK_PROFILE_BUF_LEN-1)]+=static_cast<Sint64>(endTick) - static_cast<Sint64>(startTick);
		stepCounter++;
	}
}

void Game::dirtyWarFlagGradient(void)
{
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
		teams[i]->dirtyWarFlagGradient();
}
