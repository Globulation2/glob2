// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2007 Bradley Arsenault

#include "GameEvent.h"

#include <utility>

#include "FormatableString.h"
#include "IntBuildingType.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "UnitConsts.h"

using namespace GAGCore;

GameEvent::GameEvent(GameEventType type, Uint32 step, Sint16 x, Sint16 y, Uint32 typeNum, std::string teamName)
	: type(type), step(step), x(x), y(y), typeNum(typeNum), teamName(std::move(teamName))
{
}

GameEvent GameEvent::unitUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint32 unitType)
{
	return GameEvent(GEUnitUnderAttack, step, x, y, unitType, std::string());
}

GameEvent GameEvent::unitLostConversion(Uint32 step, Sint16 x, Sint16 y, std::string teamName)
{
	return GameEvent(GEUnitLostConversion, step, x, y, 0, std::move(teamName));
}

GameEvent GameEvent::unitGainedConversion(Uint32 step, Sint16 x, Sint16 y, std::string teamName)
{
	return GameEvent(GEUnitGainedConversion, step, x, y, 0, std::move(teamName));
}

GameEvent GameEvent::buildingUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint8 buildingType)
{
	return GameEvent(GEBuildingUnderAttack, step, x, y, buildingType, std::string());
}

GameEvent GameEvent::buildingCompleted(Uint32 step, Sint16 x, Sint16 y, Uint8 buildingType)
{
	return GameEvent(GEBuildingCompleted, step, x, y, buildingType, std::string());
}

std::string GameEvent::formatMessage() const
{
	StringTable* table = Toolkit::getStringTable();
	switch (type)
	{
	case GEUnitUnderAttack:
		return FormatableString(table->getString("[Your %0 are under attack]"))
		           .arg(getUnitName(typeNum));
	case GEUnitLostConversion:
		return FormatableString(table->getString("[Your unit got converted to %0's team]"))
		           .arg(teamName);
	case GEUnitGainedConversion:
		return FormatableString(table->getString("[%0's team unit got converted to your team]"))
		           .arg(teamName);
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
