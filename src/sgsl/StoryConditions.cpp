// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file StoryConditions.cpp
	\brief SGSL wait conditions: game-state queries a Story blocks on
*/

#include <iostream>
#include <optional>
#include <string>

#include "Game.h"
#include "SGSL.h"
#include "Unit.h"

//get values from the game
int Story::valueOfVariable(const Game *game, SGSLToken::TokenType type, int teamNumber, int level)
{
	TeamStat *latestStat=game->teams[teamNumber]->stats.getLatestStat();
	switch(type)
	{
		case(SGSLToken::S_WORKER):
			return latestStat->numberUnitPerType[0];
		case(SGSLToken::S_EXPLORER):
			return latestStat->numberUnitPerType[1];
		case(SGSLToken::S_WARRIOR):
			return latestStat->numberUnitPerType[2];
		case(SGSLToken::S_SWARM_B):
			return latestStat->numberBuildingPerTypePerLevel[0][level];
		case(SGSLToken::S_FOOD_B):
			return latestStat->numberBuildingPerTypePerLevel[1][level];
		case(SGSLToken::S_HEALTH_B):
			return latestStat->numberBuildingPerTypePerLevel[2][level];
		case(SGSLToken::S_WALKSPEED_B):
			return latestStat->numberBuildingPerTypePerLevel[3][level];
		case(SGSLToken::S_SWIMSPEED_B):
			return latestStat->numberBuildingPerTypePerLevel[4][level];
		case(SGSLToken::S_ATTACK_B):
			return latestStat->numberBuildingPerTypePerLevel[5][level];
		case(SGSLToken::S_SCIENCE_B):
			return latestStat->numberBuildingPerTypePerLevel[6][level];
		case(SGSLToken::S_DEFENCE_B):
			return latestStat->numberBuildingPerTypePerLevel[7][level];
		// The flag and late-building tokens are NOT contiguous with
		// S_SWARM_B..S_DEFENCE_B (see the token enum in SGSL.h), so they need
		// their own cases; the indices are the IntBuildingType short numbers.
		// The parser accepts any token in [S_WORKER, S_MARKET_B] as a
		// wait-condition variable, so every one of them must be covered here.
		case(SGSLToken::S_EXPLOR_F):
			return latestStat->numberBuildingPerTypePerLevel[8][level];
		case(SGSLToken::S_FIGHT_F):
			return latestStat->numberBuildingPerTypePerLevel[9][level];
		case(SGSLToken::S_CLEARING_F):
			return latestStat->numberBuildingPerTypePerLevel[10][level];
		case(SGSLToken::S_WALL_B):
			return latestStat->numberBuildingPerTypePerLevel[11][level];
		case(SGSLToken::S_MARKET_B):
			return latestStat->numberBuildingPerTypePerLevel[12][level];
		default:
			assert(false);
			return 0;
	}
}

// code for testing conditions. If readLevel is true, for building check for specific level. If atMin is true, add higher levels too
bool Story::conditionTester(const Game *game, int pc, bool readLevel, bool only)
{
	SGSLToken::TokenType type, operation;
	int level, teamNumber, amount;

	type = line[pc++].type;
	teamNumber = line[pc++].value;
	if (readLevel)
		level = line[pc++].value;
	else
		level = -1;
	operation = line[pc++].type;
	amount = line[pc].value;
	
	// if we want all unit over one level, sum
	int val = 0;
	if (!only)
		for (int i = level; i < 6; i++)
			val += valueOfVariable(game, type, teamNumber, i);
	else
		val = valueOfVariable(game, type, teamNumber, level);
		
	switch (operation)
	{
		case (SGSLToken::S_HIGHER):
		{
			if (verbose)
			std::cout << "Story::conditionTester : SGSL thread " << this << " testing "
				  << SGSLToken::getNameByType(type) << " ("
				  << teamNumber << ", " << level << ") : "
				  << val << " >? " << amount << std::endl;
			return (val > amount);
		}
		case (SGSLToken::S_LOWER):
		{
			if (verbose)
				std::cout << "Story::conditionTester : SGSL thread " << this << " testing "
				  << SGSLToken::getNameByType(type) << " ("
					  << teamNumber << ", " << level << ") : "
					  << val << " <? " << amount << std::endl;
			return (val < amount);
		}
		case (SGSLToken::S_EQUAL):
		{
			if (verbose)
				std::cout << "Story::conditionTester : SGSL thread " << this << " testing "
					  << SGSLToken::getNameByType(type) << " ("
					  << teamNumber << ", " << level << ") : "
					  << val << " =? " << amount << std::endl;
			return (val == amount);
		}
		default:
			return false;
	}
}

//! Reads the tokens of an area("name", who) wait condition starting at execLine,
//! leaving execLine on the condition's last token, and tells whether any unit
//! of a team selected by "who" stands in the area.
bool Story::areaContainsUnit(const Game *game, int &execLine) const
{
	std::string areaName=line[execLine].msg;

	execLine++;
	Uint32 teamsToTestMask = 0;
	//A team number is given
	if (line[execLine].type==SGSLToken::INT)
	{
		teamsToTestMask = 1<<(line[execLine].value);
	}
	//All of the enemies are given
	else if (line[execLine].type==SGSLToken::S_ENEMY)
	{
		execLine++;
		teamsToTestMask = game->teams[line[execLine].value]->enemies;
	}
	//All of the allies are given
	else if (line[execLine].type==SGSLToken::S_ALLY)
	{
		execLine++;
		teamsToTestMask = game->teams[line[execLine].value]->allies;
	}
	else
		assert(false);

	const std::optional<int> areaN = mapAreaNumber(game, areaName);
	bool foundUnit=false;
	//There isn't a map script area with the same name, try the old map scripts
	if(!areaN)
	{
		AreaMap::const_iterator fi;
		if ((fi = mapscript->areas.find(line[execLine].msg)) == mapscript->areas.end())
			assert(false);

		int x = fi->second.x;
		int y = fi->second.y;
		int r = fi->second.r;
		int dx, dy;
		for (dy=y-r; dy<y+r && !foundUnit; dy++)
		{
			for (dx=x-r; dx<x+r && !foundUnit; dx++)
			{
				Uint16 gid=game->map.getGroundUnit(dx, dy);
				if (gid!=NOGUID)
				{
					int team=Unit::GIDtoTeam(gid);
					if ((1<<team) & teamsToTestMask)
					{
						foundUnit = true;
					}
				}
			}
		}
	}
	//There is a map script area with the same name, check the positions
	else
	{
		for(int x=0; x<game->map.getW() && !foundUnit; ++x)
		{
			for(int y=0; y<game->map.getH() && !foundUnit; ++y)
			{
				if(game->map.isPointSet(*areaN, x, y))
				{
					Uint16 gid=game->map.getGroundUnit(x, y);
					if (gid!=NOGUID)
					{
						int team=Unit::GIDtoTeam(gid);
						if ((1<<team) & teamsToTestMask)
						{
							foundUnit = true;
						}
					}
				}
			}
		}
	}
	return foundUnit;
}

//! Evaluates the wait(...) statement at lineSelector. Returns true when the
//! story may move past it, having advanced lineSelector to the statement's
//! last token; returns false while the story must keep waiting.
bool Story::waitConditionMet(Game *game)
{
	bool negate = false;
	int execLine = lineSelector+1;

	if (line[execLine].type == SGSLToken::S_NOT)
	{
		negate = true;
		execLine++;
	}
	switch (line[execLine].type)
	{
		case (SGSLToken::INT):
		{
			// The idea is to put an int token on execution which stands for decrement and waiting
			internTimer = line[execLine].value;
			lineSelector = execLine;
			return false;
		}
		case (SGSLToken::S_ISDEAD):
		{
			execLine++;
			if (!game->teams[line[execLine].value]->isAlive)
			{
				lineSelector = execLine;
				return true;
			}
			else
				return false;
		}
		case (SGSLToken::S_AREA):
		{
			execLine++;
			const bool foundUnit = areaContainsUnit(game, execLine);
			if (foundUnit != negate)
			{
				lineSelector = execLine;
				return true;
			}
			else
			{
				return false;
			}
		}
		case (SGSLToken::S_WORKER):
		case (SGSLToken::S_EXPLORER):
		case (SGSLToken::S_WARRIOR):
		{
			bool conditionResult = conditionTester(game, execLine, false, true);
			conditionResult ^= negate;
			if (conditionResult)
			{
				lineSelector += 4;
				lineSelector += negate ? 1 : 0;
				return true;
			}
			else
				return false;
		}
		default: //Test conditions
		{
			// Check if we have the "atmin" keyword
			bool only = false;
			if (line[execLine].type == SGSLToken::S_ONLY)
			{
				only = true;
				execLine++;
			}
			
			// Do the test
			bool conditionResult = conditionTester(game, execLine, true, only);
			conditionResult ^= negate;
			if (conditionResult)
			{
				lineSelector += 5;
				lineSelector += negate ? 1 : 0;
				lineSelector += only ? 1 : 0;
				return true;
			}
			else
				return false;
		}
	}
}
