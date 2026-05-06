// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include <string>

#include "GraphicContext.h"

enum GameEventType
{
	GEUnitUnderAttack = 0,
	GEUnitLostConversion,
	GEUnitGainedConversion,
	GEBuildingUnderAttack,
	GEBuildingCompleted,
	GESize,
};

/// An in-game notification (unit/building under attack, conversion, building
/// completed). Pushed onto Team::events by the simulation; consumed by the GUI
/// to show a colored chat message and let the player jump to the location.
class GameEvent
{
public:
	static GameEvent unitUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint32 unitType);
	static GameEvent unitLostConversion(Uint32 step, Sint16 x, Sint16 y, std::string teamName);
	static GameEvent unitGainedConversion(Uint32 step, Sint16 x, Sint16 y, std::string teamName);
	static GameEvent buildingUnderAttack(Uint32 step, Sint16 x, Sint16 y, Uint8 buildingType);
	static GameEvent buildingCompleted(Uint32 step, Sint16 x, Sint16 y, Uint8 buildingType);

	std::string formatMessage() const;
	GAGCore::Color formatColor() const;

	GameEventType getEventType() const { return type; }
	Uint32 getStep() const { return step; }
	Sint16 getX() const { return x; }
	Sint16 getY() const { return y; }

private:
	GameEvent(GameEventType type, Uint32 step, Sint16 x, Sint16 y, Uint32 typeNum, std::string teamName);

	GameEventType type;
	Uint32 step;
	Sint16 x;
	Sint16 y;
	// Unit type for GEUnitUnderAttack; building shortTypeNum for GEBuildingUnderAttack
	// and GEBuildingCompleted; unused for conversion events.
	Uint32 typeNum;
	// Other team's first player name for conversion events; empty otherwise.
	std::string teamName;
};
