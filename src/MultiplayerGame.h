/*
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

#ifndef __MultiplayerGame_h
#define __MultiplayerGame_h

#include "YOGClient.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include "NetEngine.h"

///This class represents a multi-player game, both in the game and while waiting for players
///and setting up options. It channels its information through a YOGClient
class MultiplayerGame
{
public:
	///Creates a game instance and links it with the provided YOGClient
	MultiplayerGame(boost::shared_ptr<YOGClient> client);
	
	///Should be called frequently
	void update();
	
	///Attempt to create a new game on the server with the given name, and wait for reply
	void createNewGame(const std::string& name);
	
	///Attempt to join an existing game on the server with the given id, 
	void joinGame(Uint16 gameID);

	///Represents the state of joining or creating a game	
	enum GameJoinCreationState
	{
		HostingGame,
		JoinedGame,
		WaitingForCreateReply,
		WaitingForJoinReply,
		NothingYet,
	};
	
	///Returns the current state of joining or creating a game
	GameJoinCreationState getGameJoinCreationState() const;
	
	///Returns the reason the creation of a game was refused
	YOGGameCreateRefusalReason getGameCreationState();

	///Returns the reason the joining of a game was refused
	YOGGameJoinRefusalReason getGameJoinState();

	///Sets the map header
	void setMapHeader(MapHeader& mapHeader);
	
	///Returns the map header
	MapHeader& getMapHeader();

	///Returns the game header. It can be modified. After modifying it,
	///one must call updateGameHeader(). At no point should any changes
	///be done to the base players, they are managed by the server
	GameHeader& getGameHeader();
	
	///Call this to send the the changes of the game header to the server
	void updateGameHeader();
	
	///Tells whether the list of players has changed since the last call to this function
	bool hasPlayersChanged();
	
	///Sets the assocciatted net engine to push recieved orders into
	void setNetEngine(NetEngine* engine);
	
	///Sends the given Order across the network
	void pushOrder(shared_ptr<Order> order, int playerNum, int ustep);
	
	///Causes the game to be started
	void startGame();
	
	///This says whether the game is ready to start
	bool isGameReadyToStart();
protected:
	friend class YOGClient;
	///This receives a message that is sent to the game
	void recieveMessage(boost::shared_ptr<NetMessage> message);
	///Adds a person to the gameHeader
	void addPerson(Uint16 playerID);
	///Removes a person from the gameHeader
	void removePerson(Uint16 playerID);
	
	///This will start the game
	void startEngine();
	
	int getLocalPlayer();
private:
	boost::shared_ptr<YOGClient> client;
	GameJoinCreationState gjcState;
	YOGGameCreateRefusalReason creationState;
	YOGGameJoinRefusalReason joinState;
	MapHeader mapHeader;
	GameHeader gameHeader;
	bool playersChanged;
	NetEngine* netEngine;
	boost::shared_ptr<MapAssembler> assembler;
};


#endif
