/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __ENGINE_H
#define __ENGINE_H

#include "Header.h"
#include "GameGUI.h"
#include <string>
#include "Campaign.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include "NetEngine.h"
#include "MultiplayerGame.h"
#include "CPUStatisticsManager.h"


class MultiplayersJoin;
class NetGame;

/// Engine is the backend of the game. It is responsible for loading and setting up games and players,
/// and its run function is meant to run the game that has been loaded.
class Engine
{
public:
	//! Constructor
	Engine();
	//! Destructor
	~Engine();

	/// Initiates a campaign map. This first loads the MapHeader, and then generates a GameHeader for
	/// the campaign map. It then informs GameGUI that this map is a campaign, and if the player wins
	/// it, the given Campaign should be informed. 
	int initCampaign(const std::string &mapName, Campaign& campaign, const std::string& missionName);

	/// Initiates a campaign game that isn't part of a campaign. One example is the tutorial, which
	/// is a lone map that runs with campaign semantics
	int initCampaign(const std::string &mapName);

	/// Displays the CustomMap dialogue, and initiates a game from the settings it recieves
	int initCustom();

	/// Initiate a custom game from the provided game, without adjusting settings from the user
	int initCustom(const std::string &gameName);

	/// Show the load/save dialoge, and use initCustom(gameName) to load the game
	int initLoadGame();

	/// Initiate a game with the given MultiplayerGame
	int initMultiplayer(boost::shared_ptr<MultiplayerGame> multiplayerGame, boost::shared_ptr<YOGClient> client, int localPlayer);

	//! This function creates a game with a random map and random AI for every team
	void createRandomGame();
	
	///Tells whether a map matching mapHeader is located on this system
	bool haveMap(const MapHeader& mapHeader);

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
	
private:
	/// Initiates a game, provided the map and game header. This initiates the net
	/// as well. When setGameHeader is true, the gameHeader given will replace the
	/// one loaded with the map. When ignore GUI info is set, the game will ignore
	/// GameGUI data in the file, such as viewport position and localTeam. This is
	/// needed for when your loading a save game over the internet
	int initGame(MapHeader& mapHeader, GameHeader& gameHeader, bool setGameHeader=true, bool ignoreGUIData=false);

	/// Prepares a GameHeader for the given mapHeader as a campaign map
	/// Campaign maps have one player per team, and the player can be
	/// either a human or an AI. AI's are all AINull. When the human
	/// is found, the player number is put in localPlayer, and the
	/// team number is put in localTeam
	GameHeader prepareCampaign(MapHeader& mapHeader, int& localPlayer, int& localTeam);

	//! Load a game. Return true on success
	bool loadGame(const std::string &filename);
	//! Do the final adjustements, like setting local teams and viewport, rendering minimap
	void finalAdjustements(void);

	///This will load the map header of the game with the given filename
	MapHeader loadMapHeader(const std::string &filename);

	///This will load the game header of the game with the given filename
	GameHeader loadGameHeader(const std::string &filename);

	///This function will choose a random map from the available maps
	MapHeader chooseRandomMap();
	
	///This function prepares a random set of AI's in a GameHeader, first player is always human + ai team
	GameHeader createRandomGame(int numberOfTeams);

	//! The GUI, contains the whole game also
	GameGUI gui;
	//! The netGame, take care of order queuing and dispatching
	NetEngine *net;
	//! The MultiplayerGame, recieves orders from across a network
	shared_ptr<MultiplayerGame> multiplayer;

	CPUStatisticsManager cpuStats;

	Sint32 noxStartTick, noxEndTick;

	FILE *logFile;

	static const bool verbose = false;
};

#endif
