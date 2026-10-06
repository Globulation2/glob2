// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <map>
#include <string>
#include "Types.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
};


class Game;

///This class manages the default number of units to be assigned when constructing a new buildings
class GameGUIDefaultAssignManager
{
public:
	///Constructs a GameGUIDefaultAssignManager
	explicit GameGUIDefaultAssignManager(Game& game);
	
	///Retrieve the default assigned units for a given building typenum (note, not the 
	///ntBuildingType typenum, the BuildingTypes typenum)
	int getDefaultAssignedUnits(int typenum);
	
	///Sets the default assigned units for a given building typenum
	void setDefaultAssignedUnits(int typenum, int value);

	////Saves the default assign information
	void save(GAGCore::OutputStream* stream) const;

	///Loads the default assign information
	void load(GAGCore::InputStream* stream, Sint32 versionMinor);
	
private:
	Game& game;
	std::map<std::string, int> unitCount;
};


