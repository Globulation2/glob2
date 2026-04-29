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

#include "BuildingType.h"
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

Game::Game(GameGUI *gui, MapEdit* edit):
	mapscript(gui)
{
	logFile = globalContainer->logFileManager->getFile("Game.log");

	init(gui, edit);
}

Game::~Game()
{
	int sum=0;
	for (int i=0; i<Team::MAX_COUNT; i++)
		sum+=ticksGameSum[i];
	if (sum)
	{
		fprintf(logFile, "(sync)stepCounter=%d\n", stepCounter);
		fprintf(logFile, "execution time of Game::step: sum=%d\n", sum);
		for (int i=0; i<Team::MAX_COUNT; i++)
			fprintf(logFile, "ticksGameSum[%2d]=%8d, (%f %%)\n", i, ticksGameSum[i], (float)ticksGameSum[i]*100./(float)sum);
		fprintf(logFile, "\n");
		for (int i=0; i<Team::MAX_COUNT; i++)
		{
			fprintf(logFile, "ticksGameSum[%2d]=", i);
			for (int j=0; j<(int)(0.5+(float)ticksGameSum[i]*100./(float)sum); j++)
				fprintf(logFile, "*");
			fprintf(logFile, "\n");
		}
	}

	overlayAlphas.resize(0);

	clearGame();

	delete globalContainer->replayWriter;
	globalContainer->replayWriter = NULL;
}

void Game::init(GameGUI *gui, MapEdit* edit)
{
	this->gui=gui;
	this->edit=edit;
	buildProjects.clear();

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

	for (int i=0; i<Team::MAX_COUNT; i++)
		ticksGameSum[i]=0;

	anyPlayerWaitedTimeFor = 0;
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
		teams[players[i]->teamNumber]->numberOfPlayer+=1;
		teams[players[i]->teamNumber]->playersMask|=(1<<i);
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
	Uint32 oldMask = maskAwayPlayer;
	maskAwayPlayer = mask;

	if(mask != 0)
	{
		if(oldMask == 0)
			anyPlayerWaitedTimeFor = 0;
		anyPlayerWaited = true;
	}
	else
	{
		anyPlayerWaited = false;
	}
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
