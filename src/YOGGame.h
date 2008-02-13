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

#ifndef __YOGGame_h
#define __YOGGame_h

#include "YOGPlayer.h"
#include "MapHeader.h"
#include <boost/shared_ptr.hpp>
#include "YOGGamePlayerManager.h"

class YOGMapDistributor;
class YOGGameServer;
class YOGGamePlayerManager;

///This handles a "game" from the server's point of view. This means that it handles
///routing between clients, holding the map and game data, etc..
class YOGGame
{
public:
	///Constructs a new YOG game
	YOGGame(Uint16 gameID, Uint32 chatChannel, YOGGameServer& server);

	///Updates the game
	void update();

	///Adds the player to the game
	void addPlayer(shared_ptr<YOGPlayer> player);

	///Adds an AI to the game
	void addAIPlayer(AI::ImplementitionID type);

	///Removes the player from the game
	void removePlayer(shared_ptr<YOGPlayer> player);

	///Removes the AI from the game
	void removeAIPlayer(int playerNum);

	///Sets the team of the player
	void setTeam(int playerNum, int teamNum);

	///Sets the host of the game
	void setHost(shared_ptr<YOGPlayer> player);

	///Sets the map header of the game
	void setMapHeader(const MapHeader& mapHeader);

	///Gets the game header of the game
	GameHeader& getGameHeader();

	///Routes the given message to all players except for the sender,
	///unless sender is null
	void routeMessage(shared_ptr<NetMessage> message, shared_ptr<YOGPlayer> sender=shared_ptr<YOGPlayer>());
	
	///Routes the given order to all players except the sender. Sender can be null
	void routeOrder(shared_ptr<NetSendOrder> order, shared_ptr<YOGPlayer> sender=shared_ptr<YOGPlayer>());
	
	///Returns the map distributor
	shared_ptr<YOGMapDistributor> getMapDistributor();
	
	///Kicks the player and sends a kick message to the player
	void kickPlayer(shared_ptr<NetKickPlayer> message);
	
	///Returns whether there are no players left in the game
	bool isEmpty() const;
	
	///Returns the game ID
	Uint16 getGameID() const;

	///Sends that a player is ready to start
	void setReadyToStart(int playerID);

	///Sends that a player is not ready to start
	void setNotReadyToStart(int playerID);

	///Recieves a game start request, refuses to the host if not all the players are ready
	///While the host is normally updated with this information, lag from the connection
	///May cause the host to start the game just as another player has joined
	void recieveGameStartRequest();
	
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

private:
	bool requested;
	bool gameStarted;
	bool oldReadyToLaunch;
	int latencyMode;
	int latencyUpdateTimer;
	MapHeader mapHeader;
	GameHeader gameHeader;
	Uint16 gameID;
	Uint32 chatChannel;
	shared_ptr<YOGPlayer> host;
	shared_ptr<YOGMapDistributor> distributor;
	std::vector<shared_ptr<YOGPlayer> > players;
	YOGGameServer& server;
	YOGGamePlayerManager playerManager;
};


#endif
