// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2007 Bradley Arsenault

#include "GameEvent.h"

#include "FormatableString.h"
#include "Game.h"
#include "IntBuildingType.h"
#include "StringTable.h"
#include "Team.h"
#include "TeamDisplay.h"
#include "Toolkit.h"
#include "UnitDisplayNames.h"

using namespace GAGCore;

GameEvent::GameEvent(GameEventType type, Uint32 step, Sint16 x, Sint16 y, Uint32 typeNum, Uint8 otherTeamNumber)
	: type(type), step(step), x(x), y(y), typeNum(typeNum), otherTeamNumber(otherTeamNumber)
{
}

GameEvent GameEvent::unitUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint32 unitType)
{
	return GameEvent(GEUnitUnderAttack, step, x, y, unitType, 0);
}

GameEvent GameEvent::unitLostConversion(Uint32 step, Sint16 x, Sint16 y, Uint8 otherTeamNumber)
{
	return GameEvent(GEUnitLostConversion, step, x, y, 0, otherTeamNumber);
}

GameEvent GameEvent::unitGainedConversion(Uint32 step, Sint16 x, Sint16 y, Uint8 otherTeamNumber)
{
	return GameEvent(GEUnitGainedConversion, step, x, y, 0, otherTeamNumber);
}

GameEvent GameEvent::buildingUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint8 buildingType)
{
	return GameEvent(GEBuildingUnderAttack, step, x, y, buildingType, 0);
}

GameEvent GameEvent::buildingCompleted(Uint32 step, Sint16 x, Sint16 y, Uint8 buildingType)
{
	return GameEvent(GEBuildingCompleted, step, x, y, buildingType, 0);
}

std::string GameEvent::formatMessage(const Game& game) const
{
	StringTable* table = Toolkit::getStringTable();
	switch (type)
	{
	case GEUnitUnderAttack:
		return FormatableString(table->getString("[Your %0 are under attack]"))
		           .arg(getUnitName(typeNum));
	case GEUnitLostConversion:
		return FormatableString(table->getString("[Your unit got converted to %0's team]"))
		           .arg(displayPlayerName(*game.teams[otherTeamNumber]));
	case GEUnitGainedConversion:
		return FormatableString(table->getString("[%0's team unit got converted to your team]"))
		           .arg(displayPlayerName(*game.teams[otherTeamNumber]));
	case GEBuildingUnderAttack:
	{
		std::string key = "[the ";
		key += IntBuildingType::typeFromShortNumber(typeNum);
		key += " is under attack]";
		return table->getString(key.c_str());
	}
	case GEBuildingCompleted:
	{
		std::string key = "[the ";
		key += IntBuildingType::typeFromShortNumber(typeNum);
		key += " is finished]";
		return table->getString(key.c_str());
	}
	case GESize:
		break;
	}
	return std::string();
}

GAGCore::Color GameEvent::formatColor() const
{
	switch (type)
	{
	case GEUnitUnderAttack:      return GAGCore::Color(200, 30, 30);
	case GEUnitLostConversion:   return GAGCore::Color(140, 0, 0);
	case GEUnitGainedConversion: return GAGCore::Color(100, 255, 100);
	case GEBuildingUnderAttack:  return GAGCore::Color(255, 0, 0);
	case GEBuildingCompleted:    return GAGCore::Color(30, 255, 30);
	case GESize:                 break;
	}
	return GAGCore::Color();
}
