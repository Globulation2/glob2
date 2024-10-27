/*
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

#ifndef __YOGServerGame_h
#define __YOGServerGame_h

#include "MapHeader.h"
#include <boost/shared_ptr.hpp>
#include "NetGamePlayerManager.h"
#include "NetReteamingInformation.h"
#include "YOGGameResults.h"

class NetKickPlayer;
class NetSendOrder;
class YOGServer;
class YOGServerGamePlayerManager;
class YOGServerFileDistributor;
class YOGServerPlayer;
class NetMessage;

///This handles a "game" from the server's point of view. This means that it handles
///routing between clients, holding the map and game data, etc..
class YOGServerGame
{
public:
	///Constructs a new YOG game
	YOGServerGame(Uint16 gameID, Uint32 chatChannel, const std::string& routerIP, YOGServer& server);

	///Updates the game
	void update();

	///Adds the player to the game
	void addPlayer(boost::shared_ptr<YOGServerPlayer> player);

	///Adds an AI to the game
	void addAIPlayer(AI::ImplementationID type);

	///Removes the player from the game
	void removePlayer(boost::shared_ptr<YOGServerPlayer> player);

	///Removes the AI from the game
	void removeAIPlayer(int playerNum);

	///Sets the team of the player
	void setTeam(int playerNum, int teamNum);

	///Sets the host of the game
	void setHost(boost::shared_ptr<YOGServerPlayer> player);

	///Sets the map header of the game
	void setMapHeader(const MapHeader& mapHeader);

	///Sets the re-teaming information of the game
	void setReTeamingInfo(const NetReTeamingInformation& reTeamingInfo);

	///Gets the game header of the game
	GameHeader& getGameHeader();

	///Routes the given message to all players except for the sender,
	///unless sender is null
	void routeMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> sender=boost::shared_ptr<YOGServerPlayer>());
	
	///Kicks the player and sends a kick message to the player
	void kickPlayer(boost::shared_ptr<NetKickPlayer> message);
	
	///Returns whether there are no players left in the game
	bool isEmpty() const;
	
	///Returns the game ID
	Uint16 getGameID() const;

	///Sends that a player is ready to start
	void setReadyToStart(int playerID);

	///Sends that a player is not ready to start
	void setNotReadyToStart(int playerID);

	///Receives a game start request, refuses to the host if not all the players are ready
	///While the host is normally updated with this information, lag from the connection
	///May cause the host to start the game just as another player has joined
	void receiveGameStartRequest();
	
	///Starts the game
	void startGame();

	///Returns the chat channel for this game
	Uint32 getChatChannel() const;

	///Returns whether the game is already running or not
	bool hasGameStarted() const;

	///Returns the hosts ID
	Uint16 getHostPlayerID() const;

	///This chooses a latency mode and sends it to all the players
	void chooseLatencyMode();
	
	///This sets a players game result
	void setPlayerGameResult(boost::shared_ptr<YOGServerPlayer> sender, YOGGameResult result);

	///This sends the games results to the game log, if this game actually went through
	void sendGameResultsToGameLog();
	
	///Returns the router ip address for this game
	const std::string getRouterIP() const;
	
	///Returns the file transfer id for the map of this game
	Uint16 getFileID() const;
private:
	bool gameStarted;
	bool hasAddedHost;
	bool oldReadyToLaunch;
	bool receivedMapHeader;
	bool requested;
	boost::shared_ptr<YOGServerPlayer> host;
	GameHeader gameHeader;
	int latencyMode;
	Uint64 latencyUpdateTimer;
	Uint32 mapFile;
	MapHeader mapHeader;
	NetGamePlayerManager playerManager;
	NetReTeamingInformation reTeamingInfo;
	std::vector<boost::shared_ptr<YOGServerPlayer> > players;
	Uint16 gameID;
	Uint32 chatChannel;
	Uint8 aiNum;
	std::string routerIP;
	YOGServer& server;
	YOGGameResults gameResults;
};


#endif
