// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "WinningConditions.h"
#include "Game.h"
#include <algorithm>
#include "Stream.h"

namespace
{
	bool teamsAreMutuallyAllied(const Game* game, int a, int b)
	{
		const Uint32 aInBsAllies = game->teams[a]->me & game->teams[b]->allies;
		const Uint32 bInAsAllies = game->teams[b]->me & game->teams[a]->allies;
		return aInBsAllies && bInAsAllies;
	}

	int maximumPrestige(const Game* game)
	{
		int maximum = 0;
		for (int i = 0; i < game->mapHeader.getNumberOfTeams(); ++i)
			maximum = std::max(maximum, game->teams[i]->prestige);
		return maximum;
	}

	template <class T>
	std::shared_ptr<WinningCondition> decodeAs(GAGCore::InputStream* stream, Uint32 versionMinor)
	{
		auto condition = std::make_shared<T>();
		condition->decodeData(stream, versionMinor);
		return condition;
	}
}

std::shared_ptr<WinningCondition> WinningCondition::getWinningCondition(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	if (stream->isEndOfStream())
		return std::shared_ptr<WinningCondition>();

	Uint8 type = stream->readUint8("type");

	switch (type)
	{
		case WCDeath:             return decodeAs<WinningConditionDeath>(stream, versionMinor);
		case WCAllies:            return decodeAs<WinningConditionAllies>(stream, versionMinor);
		case WCPrestige:          return decodeAs<WinningConditionPrestige>(stream, versionMinor);
		case WCScript:            return decodeAs<WinningConditionScript>(stream, versionMinor);
		case WCOpponentsDefeated: return decodeAs<WinningConditionOpponentsDefeated>(stream, versionMinor);
		case WCUnknown:
		default:
			break;
	}
	assert(false);
	return std::shared_ptr<WinningCondition>();//to satisfy -Wall
}


std::list<std::shared_ptr<WinningCondition> > WinningCondition::getDefaultWinningConditions()
{
	return {
		std::make_shared<WinningConditionDeath>(),
		std::make_shared<WinningConditionAllies>(),
		std::make_shared<WinningConditionPrestige>(),
		std::make_shared<WinningConditionScript>(),
		std::make_shared<WinningConditionOpponentsDefeated>(),
	};
}



bool WinningConditionDeath::hasTeamWon(int team, const Game* game) const
{
	return false;
}



bool WinningConditionDeath::hasTeamLost(int team, const Game* game) const
{
	return !game->teams[team]->isAlive;
}



WinningConditionType WinningConditionDeath::getType() const
{
	return WCDeath;
}



void WinningConditionDeath::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionDeath");
	stream->writeLeaveSection();
}



void WinningConditionDeath::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionDeath");
	stream->readLeaveSection();
}



bool WinningConditionAllies::hasTeamWon(int team, const Game* game) const
{
	for(int i=0; i<game->mapHeader.getNumberOfTeams(); ++i)
	{
		if(teamsAreMutuallyAllied(game, team, i) && game->teams[i]->hasWon)
			return true;
	}
	return false;
}



bool WinningConditionAllies::hasTeamLost(int team, const Game* game) const
{
	return false;
}



WinningConditionType WinningConditionAllies::getType() const
{
	return WCAllies;
}



void WinningConditionAllies::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionAllies");
	stream->writeLeaveSection();
}



void WinningConditionAllies::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionAllies");
	stream->readLeaveSection();
}



bool WinningConditionPrestige::hasTeamWon(int team, const Game* game) const
{
	if(game->totalPrestige < game->prestigeToReach)
		return false;
	return game->teams[team]->prestige == maximumPrestige(game);
}



bool WinningConditionPrestige::hasTeamLost(int team, const Game* game) const
{
	if(game->totalPrestige < game->prestigeToReach)
		return false;
	return game->teams[team]->prestige < maximumPrestige(game);
}



WinningConditionType WinningConditionPrestige::getType() const
{
	return WCPrestige;
}



void WinningConditionPrestige::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionPrestige");
	stream->writeLeaveSection();
}



void WinningConditionPrestige::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionPrestige");
	stream->readLeaveSection();
}


bool WinningConditionScript::hasTeamWon(int team, const Game* game) const
{
#ifdef YOG_SERVER_ONLY
	// SGSL.cpp is not linked into the server; the server never calls this
	// (Team::checkWinConditions is client-only). Stub keeps the class concrete.
	(void)team;
	(void)game;
	return false;
#else
	return game->sgslScript.hasTeamWon(team);
#endif
}



bool WinningConditionScript::hasTeamLost(int team, const Game* game) const
{
#ifdef YOG_SERVER_ONLY
	(void)team;
	(void)game;
	return false;
#else
	return game->sgslScript.hasTeamLost(team);
#endif
}


WinningConditionType WinningConditionScript::getType() const
{
	return WCScript;
}



void WinningConditionScript::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionScript");
	stream->writeLeaveSection();
}



void WinningConditionScript::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionScript");
	stream->readLeaveSection();
}



bool WinningConditionOpponentsDefeated::hasTeamWon(int team, const Game* game) const
{
	for(int i=0; i<game->mapHeader.getNumberOfTeams(); ++i)
	{
		if(!teamsAreMutuallyAllied(game, team, i) && !game->teams[i]->hasLost)
			return false;
	}
	return true;
}



bool WinningConditionOpponentsDefeated::hasTeamLost(int team, const Game* game) const
{
	return false;
}



WinningConditionType WinningConditionOpponentsDefeated::getType() const
{
	return WCOpponentsDefeated;
}



void WinningConditionOpponentsDefeated::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionOpponentsDefeated");
	stream->writeLeaveSection();
}



void WinningConditionOpponentsDefeated::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionOpponentsDefeated");
	stream->readLeaveSection();
}



