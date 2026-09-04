// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGGameInfo_h
#define __YOGGameInfo_h

#include <string>
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class summarizes hosted game information on the YOG server.
///It does not include information about the game itself, just how
///its hosted, a name, and some other preamble that the GUI can use
///to filter and sort games before a user decides to join one.
class YOGGameInfo
{
public:
	///Construct an empty YOGGameInfo
	YOGGameInfo();

	///Construct a YOGGameInfo
	YOGGameInfo(const std::string& gameName, Uint16 gameID);

	///Sets the name of the game
	void setGameName(const std::string& gameName);
	
	///Returns the name of the game
	std::string getGameName() const;

	///Sets the unique game ID of the game
	void setGameID(Uint16 id);
	
	///Returns the unique game ID of the game
	Uint16 getGameID() const;

	///This enum represents the possible game states
	enum GameState
	{
		GameOpen,
		GameRunning,
	};

	///Returns the game state
	GameState getGameState() const;
	
	///Sets the game state
	void setGameState(const GameState& state);

	///Sets the number of human players joined
	void setPlayersJoined(Uint8 playersJoined);
	
	///Returns the number of human players joined
	Uint8 getPlayersJoined() const;

	///Sets the number of AI players joined
	void setAIJoined(Uint8 aiJoined);
	
	///Returns the number of AI players joined
	Uint8 getAIJoined() const;

	///Sets the name of the map
	void setMapName(const std::string& mapName);
	
	///Returns the name of the map
	std::string getMapName() const;

	///Sets the number of teams
	void setNumberOfTeams(Uint8 numberOfTeams);
	
	///Returns the name of the game
	Uint8 getNumberOfTeams() const;

	///Encodes this YOGGameInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGGameInfo
	bool operator==(const YOGGameInfo& rhs) const;
	bool operator!=(const YOGGameInfo& rhs) const;

private:
	Uint16 gameID;
	std::string gameName;
	GameState gameState;
	Uint8 playersJoined;
	Uint8 aiJoined;
	std::string mapName;
	Uint8 numberOfTeams;
};

#endif
