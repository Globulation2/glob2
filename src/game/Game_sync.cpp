// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "AICastor.h"

#include <assert.h>
#include <string.h>



#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "AIJavaScript.h"
#include "AI.h"
#include "Player.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include <SDL3/SDL.h>
#include "sim/ClientCommandSink.h"
#include "sim/ClientRequests.h"
#include "TeamStat.h"
#include "WinProbability.h"
#include <iostream>
#include <vector>


#include "Brush.h"

#include "ReplayWriter.h"
#include "ReplayReader.h"
#include "AITelemetry.h"

#define BULLET_IMGID 0

// Per-tick sync. Split out of Game.cpp.

void Game::buildProjectSyncStep(Sint32 localTeam)
{
	PERF_SCOPE_TIME(Projects);
	for (std::list<BuildProject>::iterator bpi=buildProjects.begin(); bpi!=buildProjects.end();)
	{
		int posX=bpi->posX&map.getMaskW();
		int posY=bpi->posY&map.getMaskH();
		int teamNumber=bpi->teamNumber;
		assert(teamNumber <= teamsCount());
		Sint32 typeNum=(bpi->typeNum);
		BuildingType *bt=buildingsTypes.get(typeNum);
		int w=bt->width;
		int h=bt->height;
		if (!map.isHardSpaceForBuilding(posX, posY, w, h))
		{
			for (int y=posY; y<posY+h; y++)
				for (int x=posX; x<posX+w; x++)
				{
					// Update real map
					map.removeForbidden(x, y, teamNumber);
					// Update local map
					if (teamNumber == localTeam)
						map.displayedForbiddenView.set(map.coordToIndex(x, y), false);
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
				for (int y=posY; y<posY+h; y++)
					for (int x=posX; x<posX+w; x++)
					{
						// Update real map
						map.removeForbidden(x, y, teamNumber);
						// Update local map
						if (teamNumber == localTeam)
							map.displayedForbiddenView.set(map.coordToIndex(x, y), false);
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

void Game::winProbabilitySyncStep()
{
	// Diagnostic only, and off unless GLOB2_TEAM_TIMELINE asks for it. The
	// numbers are the ones the optional win probability victory condition reads,
	// so a test can check the engine and tools/win_probability_model.py agree
	// sample by sample instead of only at the end, and a divergence between two
	// platforms shows up as a diff rather than as a desynchronised game.
	//
	// Runs on the 512-tick boundary TeamStats samples at, after the teams have
	// stepped, so it reports exactly the state the condition was evaluated on.
	if ((stepCounter & END_OF_GAME_STAT_INTERVAL_MASK) != 0)
		return;
	if (!getenv("GLOB2_TEAM_TIMELINE"))
		return;
	std::vector<int> allianceOf;
	const std::vector<WinProbability::Slot> slots = WinProbability::slotsOf(*this, allianceOf);
	const std::vector<int> chances = WinProbability::permille(slots);
	for (size_t i = 0; i < slots.size(); ++i)
		std::cout << "GLOB2_WINPROB alliance=" << i
			<< " tick=" << stepCounter
			<< " alive=" << (slots[i].alive ? 1 : 0)
			<< " permille=" << chances[i]
			<< " units=" << slots[i].units
			<< " prestige=" << slots[i].prestige
			<< " barracks=" << slots[i].barracks
			<< " explorers=" << slots[i].explorers
			<< " foodCritical=" << slots[i].foodCritical
			<< " attack=" << slots[i].attack
			<< std::endl;
}

void Game::scriptSyncStep()
{
	PERF_SCOPE_TIME(Scripts);
	// Decorative games have no client or mission script context. Normal and
	// headless Engine sessions both supply a GameGUI (the client sink), as before.
	if (!clientSink) return;
	// do a script step
	if (legacyScriptActive())
		sgslScript.syncStep(*this, *clientSink, *clientRequests);
	mapscript.syncStep(clientSink);
}

void Game::applyClientRequests()
{
	if (!clientRequests)
		return;
	// Failing-unit markers are presentation-only (never saved or checksummed);
	// only the building whose panel is open records which units failed.
	const BuildingRef observed = clientRequests->latest().observedBuilding;
	if (observed != recordingFailingUnits)
	{
		if (Building *previous = resolveBuilding(recordingFailingUnits))
			previous->setRecordFailingUnits(false);
		if (Building *next = resolveBuilding(observed))
			next->setRecordFailingUnits(true);
		recordingFailingUnits = observed;
	}
}

void Game::publishTickEvents()
{
	if (!clientEvents)
		return;
	ClientEvents::TickPulse pulse;
	pulse.tick = stepCounter;
	pulse.valid = true;
	for (int t = 0; t < mapHeader.getNumberOfTeams(); t++)
	{
		Team *team = teams[t];
		if (!team)
			continue;
		while (std::optional<GameEvent> event = team->getEvent())
			clientEvents->push(ClientEvent::TeamEvent{t, std::move(*event)});
		for (int type = 0; type < GESize; type++)
			pulse.recentEvents[t][type] = team->wasRecentEvent(static_cast<GameEventType>(type));
	}
	clientEvents->publishPulse(pulse);
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



void Game::syncStep(Sint32 localTeam, PreparationCompletion completion)
{
	const auto random = bindRandom();
	map.preparePendingGradient();
	applyClientRequests();
	if (!anyPlayerWaited)
	{
		PERF_SCOPE_TIME(Tick);
		if (globalContainer->replayWriter && globalContainer->replayWriter->isValid())
		{
			globalContainer->replayWriter->advanceStep();
		}

		Uint64 startTick=SDL_GetTicks();

		if (!map.gradientPipelineEnabled()) map.configureGradientPipeline(2, 8);
		map.advanceGradientPipeline();

		for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
			teams[i]->syncStep();

		map.syncStep(stepCounter, false);
		if (globalContainer->replaying && globalContainer->replayReader)
			globalContainer->replayReader->applyTelemetry(*this);
		if ((stepCounter & 31) == 0)
		{
			for (int t = 0; t < mapHeader.getNumberOfTeams(); ++t)
				AITelemetry::capture(teams[t], false, false);
			if (globalContainer->replayWriter)
				globalContainer->replayWriter->captureTelemetry();
		}
		// Normally polled controllers already observed this logical tick in their
		// ordered worker stream. Only replica/replay seats need this boundary.
		observeUnpolledAI();

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
			winProbabilitySyncStep();
		}

		Uint64 endTick=SDL_GetTicks();
		ticksGameSum[stepCounter&(TICK_PROFILE_BUF_LEN-1)]+=static_cast<Sint64>(endTick) - static_cast<Sint64>(startTick);
		publishTickEvents();
		stepCounter++;
		// All world mutations, including script/fog/project tail work, are done.
		// Selection stays ordered; only private preparation can join AI decisions.
		map.stagePeriodicGradientPreparation();
		if (completion == PreparationCompletion::Complete) map.preparePendingGradient();
	}
}

void Game::dirtyWarFlagGradient(void)
{
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
		teams[i]->dirtyWarFlagGradient();
}
