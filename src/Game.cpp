// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>

#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <string>

#include <FileManager.h>

#include "DatasetWriter.h"
#include "Game.h"
#include "AIMaximaStrategy.h"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "GameGUI.h"
#include "SDLCompat.h"

#include "MapEdit.h"

#include "Brush.h"
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
	clearGame();
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

	stepCounter=0;
	prestigeToReach=0;

	for (int i=0; i<TICK_PROFILE_BUF_LEN; i++)
		ticksGameSum[i]=0;

	maskAwayPlayer = 0;
}


/** Reset player and team lists, game end stuff and selection stuff. */
void Game::clearGame()
{
	resolvedMaximaStrategies.clear();
	maximaPendingHeader=nullptr;
	hasSavedRandomState = false;
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

	highlightBuildingType=0;
	highlightUnitType=0;
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
	resolvedMaximaStrategies.clear();
	maximaPendingHeader=&newGameHeader;
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

	// A loaded saved game already restored the live RNG. New maps and old
	// saves retain the seed-based initialization used by earlier versions.
	if (!hasSavedRandomState || !mapHeader.getIsSavedGame() ||
		newGameHeader.getRandomSeed() != gameHeader.getRandomSeed())
		setSyncRandSeed(newGameHeader.getRandomSeed());

	if(newGameHeader.isMapDiscovered())
		map.setMapDiscovered();

	gameHeader = newGameHeader;
	maximaPendingHeader=nullptr;
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

namespace
{
	std::string mergedMaximaOverrides(const std::string& base,
		const std::string& team, const std::string& player)
	{
		std::map<std::string, std::string> assignments;
		const std::string sources[3]={base, team, player};
		for(int source=0; source<3; ++source)
		{
			std::string normalized=sources[source];
			std::replace(normalized.begin(), normalized.end(), ';', ',');
			std::istringstream entries(normalized);
			std::string entry;
			while(std::getline(entries, entry, ','))
			{
				const size_t equals=entry.find('=');
				if(equals==std::string::npos)
				{
					if(!entry.empty()) assignments[entry]=entry;
					continue;
				}
				std::string key=entry.substr(0, equals);
				key.erase(0, key.find_first_not_of(" \t\r\n"));
				const size_t end=key.find_last_not_of(" \t\r\n");
				if(end!=std::string::npos) key.erase(end+1);
				assignments[key]=entry;
			}
		}
		std::ostringstream result;
		for(std::map<std::string, std::string>::const_iterator setting=
			assignments.begin(); setting!=assignments.end(); ++setting)
		{
			if(setting!=assignments.begin()) result<<",";
			result<<setting->second;
		}
		return result.str();
	}
}

const AIMaxima::ResolvedStrategy& Game::resolveMaximaStrategy(int playerNumber)
{
	std::map<int, std::shared_ptr<AIMaxima::ResolvedStrategy> >::const_iterator
		cached=resolvedMaximaStrategies.find(playerNumber);
	if(cached!=resolvedMaximaStrategies.end())
		return *cached->second;
	std::shared_ptr<AIMaxima::ResolvedStrategy> resolved(
		new AIMaxima::ResolvedStrategy);
	AIMaxima::StrategyConfigOptions options=globalContainer
		? globalContainer->maximaStrategyOptions
		: AIMaxima::StrategyConfigOptions();
	std::string error;
	const GameHeader* strategyHeader=maximaPendingHeader
		? maximaPendingHeader : &gameHeader;
	int teamNumber=-1;
	if(playerNumber>=0 && playerNumber<strategyHeader->getNumberOfPlayers())
		teamNumber=strategyHeader->getBasePlayer(playerNumber).teamNumber;
	std::string teamOverrides;
	std::string playerOverrides;
	if(globalContainer)
	{
		std::map<int, std::string>::const_iterator team=
			globalContainer->maximaTeamOverrides.find(teamNumber);
		if(team!=globalContainer->maximaTeamOverrides.end())
			teamOverrides=team->second;
		std::map<int, std::string>::const_iterator player=
			globalContainer->maximaPlayerOverrides.find(playerNumber);
		if(player!=globalContainer->maximaPlayerOverrides.end())
			playerOverrides=player->second;
	}
	options.inlineOverrides=mergedMaximaOverrides(options.inlineOverrides,
		teamOverrides, playerOverrides);
	if(!AIMaxima::StrategyResolver::resolve(options, strategyHeader,
		*resolved, error))
	{
		std::cerr<<"Maxima strategy error: "<<error<<std::endl;
		std::abort();
	}
	resolvedMaximaStrategies[playerNumber]=resolved;
	std::cerr<<"Maxima strategy: format="
		<<AIMaxima::StrategyResolver::formatName(resolved->format)
		<<" player="<<playerNumber<<" team="<<teamNumber<<" sources=";
	for(size_t source=0; source<resolved->sources.size(); ++source)
	{
		if(source) std::cerr<<",";
		std::cerr<<resolved->sources[source];
	}
	std::cerr<<" values="<<AIMaxima::StrategyResolver::canonicalValues(
		resolved->values)<<std::endl;
	return *resolved;
}
