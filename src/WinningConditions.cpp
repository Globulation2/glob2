// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "WinningConditions.h"
#include "Game.h"
#include "TeamStat.h"
#include "WinProbability.h"
#include <vector>
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

	// Custom-game "sudden-death timer" rule: like maximumPrestige() above, but
	// without its floor at 0. WinningConditionPrestige can share that floor
	// safely -- its own totalPrestige<prestigeToReach guard keeps it from ever
	// evaluating while every team could plausibly be negative -- but sudden
	// death's only guard is the tick count, so it must handle an all-negative
	// standing correctly: with the floor, maximum could come out as 0 even
	// though no team is actually at 0, making hasTeamWon() false and
	// hasTeamLost() true for every team simultaneously instead of the
	// intended tie among whoever is actually least-negative.
	int actualMaximumPrestige(const Game* game)
	{
		int maximum = game->teams[0]->prestige;
		for (int i = 1; i < game->mapHeader.getNumberOfTeams(); ++i)
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
		case WCSuddenDeath:       return decodeAs<WinningConditionSuddenDeath>(stream, versionMinor);
		case WCWinProbability:    return decodeAs<WinningConditionWinProbability>(stream, versionMinor);
		case WCUnknown:
		default:
			// Unrecognized tag: corrupt or truncated input, not a broken
			// invariant -- report failure via null instead of asserting.
			return std::shared_ptr<WinningCondition>();
	}
}



bool WinningCondition::loadWinningConditions(GAGCore::InputStream* stream, Uint32 versionMinor, std::list<std::shared_ptr<WinningCondition> >& conditions)
{
	stream->readEnterSection("winningConditions");
	conditions.clear();
	Uint32 size = stream->readUint32("size");
	for (Uint32 i = 0; i < size; ++i)
	{
		stream->readEnterSection(i);
		std::shared_ptr<WinningCondition> condition = getWinningCondition(stream, versionMinor);
		if (!condition)
			return false;
		conditions.push_back(condition);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
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



void WinningCondition::setPrestigeWinCondition(std::list<std::shared_ptr<WinningCondition> >& conditions, bool enabled)
{
	const auto isPrestige = [](const std::shared_ptr<WinningCondition>& condition)
	{
		return condition->getType() == WCPrestige;
	};
	const auto existing = std::find_if(conditions.begin(), conditions.end(), isPrestige);

	if (!enabled)
	{
		if (existing != conditions.end())
			conditions.erase(existing);
		return;
	}
	if (existing != conditions.end())
		return;

	// Rank types by their position in getDefaultWinningConditions so the
	// default order stays the single source of truth for evaluation priority.
	const std::list<std::shared_ptr<WinningCondition> > defaults = getDefaultWinningConditions();
	const auto defaultRank = [&defaults](WinningConditionType type) -> size_t
	{
		size_t rank = 0;
		for (const auto& condition : defaults)
		{
			if (condition->getType() == type)
				return rank;
			++rank;
		}
		return defaults.size();
	};
	const size_t prestigeRank = defaultRank(WCPrestige);
	const auto insertBefore = std::find_if(conditions.begin(), conditions.end(),
		[&](const std::shared_ptr<WinningCondition>& condition)
		{
			return defaultRank(condition->getType()) > prestigeRank;
		});
	conditions.insert(insertBefore, std::make_shared<WinningConditionPrestige>());
}



void WinningCondition::setSuddenDeathWinCondition(std::list<std::shared_ptr<WinningCondition> >& conditions, std::optional<Uint32> endStepTick)
{
	const auto isSuddenDeath = [](const std::shared_ptr<WinningCondition>& condition)
	{
		return condition->getType() == WCSuddenDeath;
	};
	const auto existing = std::find_if(conditions.begin(), conditions.end(), isSuddenDeath);

	if (!endStepTick)
	{
		if (existing != conditions.end())
			conditions.erase(existing);
		return;
	}
	if (existing != conditions.end())
	{
		static_cast<WinningConditionSuddenDeath&>(**existing).endStepTick = *endStepTick;
		return;
	}
	// Appended at the very end, unlike setPrestigeWinCondition's fixed rank:
	// this is meant to fire only as a last resort, after every other
	// condition has already had its say that tick.
	auto condition = std::make_shared<WinningConditionSuddenDeath>();
	condition->endStepTick = *endStepTick;
	conditions.push_back(condition);
}



void WinningCondition::setWinProbabilityWinCondition(std::list<std::shared_ptr<WinningCondition> >& conditions, std::optional<Uint32> thresholdPermille)
{
	const auto isWinProbability = [](const std::shared_ptr<WinningCondition>& condition)
	{
		return condition->getType() == WCWinProbability;
	};
	const auto existing = std::find_if(conditions.begin(), conditions.end(), isWinProbability);

	if (!thresholdPermille)
	{
		if (existing != conditions.end())
			conditions.erase(existing);
		return;
	}
	if (existing != conditions.end())
	{
		static_cast<WinningConditionWinProbability&>(**existing).thresholdPermille = *thresholdPermille;
		return;
	}
	// Appended last, like sudden death: if a real elimination or prestige win is
	// available on the same tick, that must be the reason the game ended.
	auto condition = std::make_shared<WinningConditionWinProbability>();
	condition->thresholdPermille = *thresholdPermille;
	conditions.push_back(condition);
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



bool WinningConditionSuddenDeath::hasTeamWon(int team, const Game* game) const
{
	if (game->stepCounter < endStepTick)
		return false;
	return game->teams[team]->prestige == actualMaximumPrestige(game);
}



bool WinningConditionSuddenDeath::hasTeamLost(int team, const Game* game) const
{
	if (game->stepCounter < endStepTick)
		return false;
	return game->teams[team]->prestige < actualMaximumPrestige(game);
}



WinningConditionType WinningConditionSuddenDeath::getType() const
{
	return WCSuddenDeath;
}



void WinningConditionSuddenDeath::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionSuddenDeath");
	stream->writeUint32(endStepTick, "endStepTick");
	stream->writeLeaveSection();
}



void WinningConditionSuddenDeath::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionSuddenDeath");
	endStepTick = stream->readUint32("endStepTick");
	stream->readLeaveSection();
}






namespace
{
	/// Which alliance slot has reached the threshold this tick, or -1.
	///
	/// Recomputed rather than cached. It runs only on the 512-tick sample
	/// boundary, over at most a handful of competitors, so the cost is nothing
	/// next to the risk: a cache is state that two machines could disagree
	/// about, and this decides the outcome of the game.
	int decidedAlliance(const Game* game, Uint32 thresholdPermille, std::vector<int>& allianceOf)
	{
		if (game->stepCounter < (Uint32)WinProbability::MINIMUM_DECISION_TICK)
			return -1;
		if ((game->stepCounter & END_OF_GAME_STAT_INTERVAL_MASK) != 0)
			return -1;
		const std::vector<WinProbability::Slot> slots = WinProbability::slotsOf(*game, allianceOf);
		return WinProbability::decided(slots, (int)thresholdPermille);
	}
}



bool WinningConditionWinProbability::hasTeamWon(int team, const Game* game) const
{
	std::vector<int> allianceOf;
	const int winner = decidedAlliance(game, thresholdPermille, allianceOf);
	return winner >= 0 && allianceOf[team] == winner;
}



bool WinningConditionWinProbability::hasTeamLost(int team, const Game* game) const
{
	// Everyone outside the called alliance has lost, otherwise nothing sets
	// isGameEnded and the game would carry on with a declared winner.
	std::vector<int> allianceOf;
	const int winner = decidedAlliance(game, thresholdPermille, allianceOf);
	return winner >= 0 && allianceOf[team] != winner;
}



WinningConditionType WinningConditionWinProbability::getType() const
{
	return WCWinProbability;
}



void WinningConditionWinProbability::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeUint8(getType(), "type");
	stream->writeEnterSection("WinningConditionWinProbability");
	stream->writeUint32(thresholdPermille, "thresholdPermille");
	stream->writeLeaveSection();
}



void WinningConditionWinProbability::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("WinningConditionWinProbability");
	thresholdPermille = stream->readUint32("thresholdPermille");
	stream->readLeaveSection();
}
