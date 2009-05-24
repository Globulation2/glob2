/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "WinningConditions.h"
#include "Game.h"
#include <algorithm>
#include "Stream.h"

boost::shared_ptr<WinningCondition> WinningCondition::getWinningCondition(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	if (stream->isEndOfStream())
		return boost::shared_ptr<WinningCondition>();
	
	Uint8 type = stream->readUint8("type");
	
	switch (type)
	{
		case WCDeath:
		{
			boost::shared_ptr<WinningConditionDeath> condition(new WinningConditionDeath);
			condition->decodeData(stream, versionMinor);
			return condition;
		}
		break;
		case WCAllies:
		{
			boost::shared_ptr<WinningConditionAllies> condition(new WinningConditionAllies);
			condition->decodeData(stream, versionMinor);
			return condition;
		}
		break;
		case WCPrestige:
		{
			boost::shared_ptr<WinningConditionPrestige> condition(new WinningConditionPrestige);
			condition->decodeData(stream, versionMinor);
			return condition;
		}
		break;
		case WCScript:
		{
			boost::shared_ptr<WinningConditionScript> condition(new WinningConditionScript);
			condition->decodeData(stream, versionMinor);
			return condition;
		}
		break;
		case WCOpponentsDefeated:
		{
			boost::shared_ptr<WinningConditionOpponentsDefeated> condition(new WinningConditionOpponentsDefeated);
			condition->decodeData(stream, versionMinor);
			return condition;
		}
		break;
		case WCUnknown:
		default:
			break;
	}
	assert(false);
	return boost::shared_ptr<WinningCondition>();//to satisfy -Wall
}


std::list<boost::shared_ptr<WinningCondition> > WinningCondition::getDefaultWinningConditions()
{
	std::list<boost::shared_ptr<WinningCondition> > conditions;
	conditions.push_back(boost::shared_ptr<WinningCondition>(new WinningConditionDeath));
	conditions.push_back(boost::shared_ptr<WinningCondition>(new WinningConditionAllies));
	conditions.push_back(boost::shared_ptr<WinningCondition>(new WinningConditionPrestige));
	conditions.push_back(boost::shared_ptr<WinningCondition>(new WinningConditionScript));
	conditions.push_back(boost::shared_ptr<WinningCondition>(new WinningConditionOpponentsDefeated));
	return conditions;
}



bool WinningConditionDeath::hasTeamWon(int team, Game* game)
{
	return false;
}



bool WinningConditionDeath::hasTeamLost(int team, Game* game)
{
	if(game->teams[team]->isAlive)
		return false;
	return true;
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



bool WinningConditionAllies::hasTeamWon(int team, Game* game)
{
	for(int i=0; i<game->mapHeader.getNumberOfTeams(); ++i)
	{
		Uint32 playerToMeAllyMask = game->teams[team]->me & game->teams[i]->allies;
		Uint32 meToPlayerAllyMask = game->teams[i]->me & game->teams[team]->allies;
		if(playerToMeAllyMask && meToPlayerAllyMask && game->teams[i]->hasWon)
		{
			return true;
		}
	}
	return false;
}



bool WinningConditionAllies::hasTeamLost(int team, Game* game)
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



bool WinningConditionPrestige::hasTeamWon(int team, Game* game)
{
	if(game->totalPrestige >= game->prestigeToReach)
	{
		int totalPrestige=0;
		int maximum = 0;
		for(int i=0; i<game->mapHeader.getNumberOfTeams(); ++i)
		{
			totalPrestige += game->teams[i]->prestige;
			maximum = std::max(maximum, game->teams[i]->prestige);
		}
	
		if(game->teams[team]->prestige == maximum)
			return true;
	}
	return false;
}



bool WinningConditionPrestige::hasTeamLost(int team, Game* game)
{
	if(game->totalPrestige >= game->prestigeToReach)
	{
		int totalPrestige=0;
		int maximum = 0;
		for(int i=0; i<game->mapHeader.getNumberOfTeams(); ++i)
		{
			totalPrestige += game->teams[i]->prestige;
			maximum = std::max(maximum, game->teams[i]->prestige);
		}
	
		if(game->teams[team]->prestige < maximum)
			return true;
	}
	return false;
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



bool WinningConditionScript::hasTeamWon(int team, Game* game)
{
	if(game->script.hasTeamWon(team))
	{
		return true;
	}
	return false;
}



bool WinningConditionScript::hasTeamLost(int team, Game* game)
{
	if(game->script.hasTeamLost(team))
	{
		return true;
	}
	return false;
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



bool WinningConditionOpponentsDefeated::hasTeamWon(int team, Game* game)
{
	bool allEnemiesLost = true;
	for(int i=0; i<game->mapHeader.getNumberOfTeams(); ++i)
	{
		Uint32 playerToMeAllyMask = game->teams[team]->me & game->teams[i]->allies;
		Uint32 meToPlayerAllyMask = game->teams[i]->me & game->teams[team]->allies;
		if((playerToMeAllyMask == 0 || meToPlayerAllyMask==0) && game->teams[i]->hasLost == false)
		{
			allEnemiesLost=false;
		}
	}
	if(allEnemiesLost)
		return true;
	return false;
}



bool WinningConditionOpponentsDefeated::hasTeamLost(int team, Game* game)
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



