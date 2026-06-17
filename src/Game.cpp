// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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

#include "BuildingType.h"
#include "DatasetWriter.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "render/UnitSkin.h"
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

#include "ReplayWriter.h"

#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

#define BULLET_IMGID 0

Game::Game(GameGUI *gui, MapEdit* edit):
	mapscript(gui)
{
	init(gui, edit);
}

Game::~Game()
{

	overlayAlphas.resize(0);

	clearGame();

	globalContainer->replayWriter.reset();
}

void Game::init(GameGUI *gui, MapEdit* edit)
{
	this->gui=gui;
	this->edit=edit;
	buildProjects.clear();

#ifndef YOG_SERVER_ONLY
	animations = std::make_unique<GameAnimations>(!globalContainer->runNoX, 0);
#endif  // !YOG_SERVER_ONLY

	mapHeader.reset();
	gameHeader.reset();

	for (int i=0; i<Team::MAX_COUNT; i++)
	{
		teams[i]=NULL;
		players[i]=NULL;
	}
	clearGame();

	mouseX=0;
	mouseY=0;

	stepCounter=0;
	prestigeToReach=0;

	for (int i=0; i<TICK_PROFILE_BUF_LEN; i++)
		ticksGameSum[i]=0;

	maskAwayPlayer = 0;
}


/** Reset player and team lists, game end stuff and selection stuff. */
void Game::clearGame()
{
	// Delete existing teams and players
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (teams[i])
		{
			delete teams[i];
			teams[i]=NULL;
		}
	}
	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		if (players[i])
		{
			delete players[i];
			players[i]=NULL;
		}
	}

	// Clear build projects
	buildProjects.clear();

	///Clears prestige
	totalPrestige=0;
	totalPrestigeReached=false;
	isGameEnded=false;

	// Clears selections
	mouseUnit = NULL;
	selectedUnit = NULL;
	selectedBuilding = NULL;

	highlightBuildingType=0;
	highlightUnitType=0;
}



void Game::setMapHeader(const MapHeader& newMapHeader)
{
	mapHeader = newMapHeader;

	// set the base team, for now the number is corect but we should check that further
	for (int i=0; i<newMapHeader.getNumberOfTeams(); i++)
		teams[i]->setBaseTeam(&newMapHeader.getBaseTeam(i));
}



// Precondition: every players[i]->teamNumber must satisfy
// 0 <= teamNumber < mapHeader.getNumberOfTeams() AND teams[teamNumber] must
// be non-null. The two in-memory callers (MapEditClicks, MapEditDialog)
// build their GameHeader from a getNumberOfTeams() loop, so they're
// well-formed by construction. The loader path (GameGUIPersistence ->
// GameHeader::load -> BasePlayer::load) bounds-checks teamNumber against
// Team::MAX_COUNT before reaching here; the assert catches the residual
// case where teamNumber is in [getNumberOfTeams(), MAX_COUNT) — a stale
// header paired with a smaller-team map.
void Game::setGameHeader(const GameHeader& newGameHeader, bool saveAI)
{
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->playersMask=0;
		teams[i]->numberOfPlayer=0;
	}

	for (int i=0; i<newGameHeader.getNumberOfPlayers(); i++)
	{
		//Don't change AI's
		if(!saveAI || gameHeader.getBasePlayer(i).type < BasePlayer::P_AI)
		{
			delete players[i];
			players[i]=new Player();
			players[i]->setBasePlayer(&newGameHeader.getBasePlayer(i), teams);
		}
		const Sint32 tn = players[i]->teamNumber;
		assert(tn >= 0 && tn < mapHeader.getNumberOfTeams());
		assert(teams[tn] != NULL);
		teams[tn]->numberOfPlayer+=1;
		teams[tn]->playersMask|=(1<<i);
	}

	setSyncRandSeed(newGameHeader.getRandomSeed());

	if(newGameHeader.isMapDiscovered())
		map.setMapDiscovered();

	gameHeader = newGameHeader;
	anyPlayerWaited=false;
}



void Game::setAlliances(void)
{
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		int allyTeam = gameHeader.getAllyTeamNumber(i);
		teams[i]->allies = 0;
		teams[i]->enemies = 0;
		for(int j=0; j<mapHeader.getNumberOfTeams(); ++j)
		{
			int otherAllyTeam = gameHeader.getAllyTeamNumber(j);
			if(allyTeam == otherAllyTeam)
			{
				teams[i]->allies |= teams[j]->me;
				teams[i]->sharedVisionOther |= teams[j]->me;
			}
			else
			{
				teams[i]->enemies |= teams[j]->me;
			}
		}
	}
}

void Game::setWaitingOnMask(Uint32 mask)
{
	maskAwayPlayer = mask;
	anyPlayerWaited = (mask != 0);
}



void Game::dumpAllData(const std::string& file)
{
	OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(file));
	if (stream->isEndOfStream())
	{
		std::cerr << "Can't dump full game memory to file "<< file << std::endl;
	}
	else
	{
		std::cerr << "Dumped full game memory to file "<< file << std::endl;
		save(stream, false, file);
	}
	delete stream;
}

Team *Game::getTeamWithMostPrestige(void)
{
	int maxPrestige=0;
	Team *maxPrestigeTeam=NULL;

	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		Team *t=teams[i];
		if (t->prestige > maxPrestige)
		{
			maxPrestigeTeam=t;
			maxPrestige=t->prestige;
		}
	}
	return maxPrestigeTeam;
}

bool Game::isPrestigeWinCondition(void)
{
	std::list<std::shared_ptr<WinningCondition> >& conditions = gameHeader.getWinningConditions();
	for(std::list<std::shared_ptr<WinningCondition> >::iterator i = conditions.begin(); i!=conditions.end(); ++i)
	{
		if((*i)->getType() == WCPrestige)
			return true;
	}
	return false;
}
