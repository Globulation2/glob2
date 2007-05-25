/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef GameEvent_h
#define GameEvent_h

#include "GraphicContext.h"

enum GameEventType
{
	GEUnitUnderAttack,
	GEUnitLostConversion,
	GEUnitGainedConversion,
	GEBuildingUnderAttack,
	GEBuildingCompleted,
	//type_append_marker
};
	
///This represents an event in the game. This includes events such as building completion,
///units being attacked, etc...
class GameEvent
{
public:
	///Constructs a GameEvent with the step and the (x,y) cordinates of the event on screen
	GameEvent(Uint32 step, Sint16 x, Sint16 y);

	virtual ~GameEvent();

	///This formats a user-readable message, including translating the message 
	virtual std::string formatMessage()=0;

	///Returns the color of the message after its formatted
	virtual GAGCore::Color formatColor()=0;
	
	///Returns the step of the event
	Uint8 getStep();
	
	///Returns the x-cordinate
	Sint16 getX();
	
	///Returns the y-cordinate
	Sint16 getY();
	
	///Returns the event type
	virtual Uint8 getEventType()=0;

private:
	Uint32 step;
	Sint16 x;
	Sint16 y;
};




class UnitUnderAttack : public GameEvent
{
public:
	///Constructs a UnitUnderAttack event
	UnitUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint32 type);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	Uint32 type;
};




class UnitLostConversion : public GameEvent
{
public:
	///Constructs a UnitLostConversion event
	UnitLostConversion(Uint32 step, Sint16 x, Sint16 y, const std::string& teamName);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	std::string teamName;
};




class UnitGainedConversion : public GameEvent
{
public:
	///Constructs a UnitGainedConversion event
	UnitGainedConversion(Uint32 step, Sint16 x, Sint16 y, const std::string& teamName);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	std::string teamName;
};




class BuildingUnderAttack : public GameEvent
{
public:
	///Constructs a BuildingUnderAttack event
	BuildingUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint8 type);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	Uint8 type;
};




class BuildingCompleted : public GameEvent
{
public:
	///Constructs a BuildingCompleted event
	BuildingCompleted(Uint32 step, Sint16 x, Sint16 y, Uint8 type);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	Uint8 type;
};



//event_append_marker



#endif
