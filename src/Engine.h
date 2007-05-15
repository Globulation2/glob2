/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __ENGINE_H
#define __ENGINE_H

#include "Header.h"
#include "GameGUI.h"
#include <string>
#include "Campaign.h"

class MultiplayersJoin;
class NetGame;

//! Engine is responsible for loading games and setting up players
class Engine
{
	static const bool verbose = false;
public:
	//! Constructor
	Engine();
	//! Destructor
	~Engine();
	
	//! Load mapName for campaign, init teams and create netGame
	int initCampaign(const std::string &mapName);
	//! Init the map from the campaign
	int initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName);
	//! Display a custom map chooser screen, init teams and create netGame
	int initCustom();
	//! Init and load a custom game from gameName. init teams and create netGame
	int initCustom(const std::string &gameName);
	//! Display a game chooser screen then call initCustom(gameName) with the selected file
	int initLoadGame();
	//! Start a multiplayer game. Init teams and create netGame
	void startMultiplayer(MultiplayersJoin *multiplayersJoin);
	//! Display a map/game chooser screen suitable for multiplayer use when hosting, than call startMultiplayer
	int initMutiplayerHost(bool shareOnYOG);
	//! Join the network game, than call startMultiplayer
	int initMutiplayerJoin();
	//! Run game. A valid gui and netGame must exists
	int run();

	//! Type of error the engine init function can return
	enum EngineError
	{
		//! success
		EE_NO_ERROR=1,
		//! user canceled init
		EE_CANCEL=2,
		//! can't load a valid map
		EE_CANT_LOAD_MAP=3,
		//! no suitable player found in the map
		EE_CANT_FIND_PLAYER=4
	};

public:
	//! The GUI, contains the whole game also
	GameGUI gui;
	//! The netGame, take care of order queuing and dispatching
	NetGame *net;
	
protected:
	//! Load a game. Return true on success
	bool loadGame(const std::string &filename);
	//! Do the final adjustements, like setting local teams and viewport, rendering minimap
	void finalAdjustements(void);

protected:
	int cpuStats[41];
	int ticksToWaitStats[41];
	unsigned cpuSumStats;
	unsigned cpuSumCountStats;
	Sint32 noxStartTick, noxEndTick;
	FILE *logFile;
};

#endif
