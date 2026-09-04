// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2007 Bradley Arsenault

#ifndef GameEvent_h
#define GameEvent_h

#include "GraphicContext.h"

enum GameEventType
{
	GEUnitUnderAttack=0,
	GEUnitLostConversion,
	GEUnitGainedConversion,
	GEBuildingUnderAttack,
	GEBuildingCompleted,
	//type_append_marker
	GESize,
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
	Uint32 getStep();
	
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




class UnitUnderAttackEvent : public GameEvent
{
public:
	///Constructs a UnitUnderAttack event
	UnitUnderAttackEvent(Uint32 step, Sint16 x, Sint16 y, Uint32 type);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	Uint32 type;
};




class UnitLostConversionEvent : public GameEvent
{
public:
	///Constructs a UnitLostConversion event
	UnitLostConversionEvent(Uint32 step, Sint16 x, Sint16 y, const std::string& teamName);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	std::string teamName;
};




class UnitGainedConversionEvent : public GameEvent
{
public:
	///Constructs a UnitGainedConversion event
	UnitGainedConversionEvent(Uint32 step, Sint16 x, Sint16 y, const std::string& teamName);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	std::string teamName;
};




class BuildingUnderAttackEvent : public GameEvent
{
public:
	///Constructs a BuildingUnderAttack event
	BuildingUnderAttackEvent(Uint32 step, Sint16 x, Sint16 y, Uint8 type);

	///This formats a user-readable message, including translating the message 
	std::string formatMessage();

	///Returns the color of the message after its formatted
	GAGCore::Color formatColor();
	
	///Returns the event type
	Uint8 getEventType();
private:
	Uint8 type;
};




class BuildingCompletedEvent : public GameEvent
{
public:
	///Constructs a BuildingCompleted event
	BuildingCompletedEvent(Uint32 step, Sint16 x, Sint16 y, Uint8 type);

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
