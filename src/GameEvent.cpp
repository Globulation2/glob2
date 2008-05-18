/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  Copyright (C) 2007 Bradley Arsenault

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

#include "GameEvent.h"
#include "Toolkit.h"
#include "StringTable.h"
#include "FormatableString.h"
#include "UnitConsts.h"
#include "IntBuildingType.h"

using namespace GAGCore;

GameEvent::GameEvent(Uint32 step, Sint16 x, Sint16 y)
	: step(step), x(x), y(y)
{

}



GameEvent::~GameEvent()
{

}



Uint32 GameEvent::getStep()
{
	return step;
}


	
Sint16 GameEvent::getX()
{
	return x;
}



Sint16 GameEvent::getY()
{
	return y;
}



UnitUnderAttackEvent::UnitUnderAttackEvent(Uint32 step, Sint16 x, Sint16 y, Uint32 type)
	: GameEvent(step, x, y), type(type)
{

}



std::string UnitUnderAttackEvent::formatMessage()
{
	std::string message;
	message+=FormatableString(Toolkit::getStringTable()->getString("[Your %0 are under attack]"))
	                         .arg(getUnitName(type));
	return message;
}



GAGCore::Color UnitUnderAttackEvent::formatColor()
{
	return GAGCore::Color(200, 30, 30);
}



Uint8 UnitUnderAttackEvent::getEventType()
{
	return GEUnitUnderAttack;
}




UnitLostConversionEvent::UnitLostConversionEvent(Uint32 step, Sint16 x, Sint16 y, const std::string& teamName)
	: GameEvent(step, x, y), teamName(teamName)
{

}



std::string UnitLostConversionEvent::formatMessage()
{
	std::string message;
	message += FormatableString(Toolkit::getStringTable()->getString("[Your unit got converted to %0's team]")).arg(teamName);
	return message;
}



GAGCore::Color UnitLostConversionEvent::formatColor()
{
	return GAGCore::Color(140, 0, 0);
}



Uint8 UnitLostConversionEvent::getEventType()
{
	return GEUnitLostConversion;
}




UnitGainedConversionEvent::UnitGainedConversionEvent(Uint32 step, Sint16 x, Sint16 y, const std::string& teamName)
	: GameEvent(step, x, y), teamName(teamName)
{

}



std::string UnitGainedConversionEvent::formatMessage()
{
	std::string message;
	message += FormatableString(Toolkit::getStringTable()->getString("[%0's team unit got converted to your team]")).arg(teamName);
	return message;
}



GAGCore::Color UnitGainedConversionEvent::formatColor()
{
	return GAGCore::Color(100, 255, 100);
}



Uint8 UnitGainedConversionEvent::getEventType()
{
	return GEUnitGainedConversion;
}




BuildingUnderAttackEvent::BuildingUnderAttackEvent(Uint32 step, Sint16 x, Sint16 y, Uint8 type)
	: GameEvent(step, x, y), type(type)
{

}



std::string BuildingUnderAttackEvent::formatMessage()
{
	std::string message;
	std::string key = "[the ";
	key += IntBuildingType::typeFromShortNumber(type);
	key += " is under attack]";
	message += Toolkit::getStringTable()->getString(key.c_str());
	return message;
}



GAGCore::Color BuildingUnderAttackEvent::formatColor()
{
	return GAGCore::Color(255, 0, 0);
}



Uint8 BuildingUnderAttackEvent::getEventType()
{
	return GEBuildingUnderAttack;
}




BuildingCompletedEvent::BuildingCompletedEvent(Uint32 step, Sint16 x, Sint16 y, Uint8 type)
	: GameEvent(step, x, y), type(type)
{

}



std::string BuildingCompletedEvent::formatMessage()
{
	std::string message;
	std::string key = "[the ";
	key += IntBuildingType::typeFromShortNumber(type);
	key += " is finished]";
	message += Toolkit::getStringTable()->getString(key.c_str());
	return message;
}



GAGCore::Color BuildingCompletedEvent::formatColor()
{
	return GAGCore::Color(30, 255, 30);
}



Uint8 BuildingCompletedEvent::getEventType()
{
	return GEBuildingCompleted;
}



///code_append_marker

