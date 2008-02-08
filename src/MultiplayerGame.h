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

#ifndef __MultiplayerGame_h
#define __MultiplayerGame_h

#include "YOGClient.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include "NetEngine.h"
#include "MultiplayerGameEventListener.h"
#include "MultiplayerGamePlayerManager.h"
#include <list>

///This class represents a multi-player game, both in the game and while waiting for players
///and setting up options. It channels its information through a YOGClient
class MultiplayerGame
{
public:
	///Creates a game instance and links it with the provided YOGClient
	MultiplayerGame(boost::shared_ptr<YOGClient> client);
	
	~MultiplayerGame();
	
	///Should be called frequently
	void update();
	
	///Attempt to create a new game on the server with the given name, and wait for reply
	void createNewGame(const std::string& name);
	
	///Attempt to join an existing game on the server with the given id, 
	void joinGame(Uint16 gameID);

	///Leaves the game you currently occupy
	void leaveGame();

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
	void pushOrder(shared_ptr<Order> order, int playerNum);
	
	///Causes the game to be started on all clients.
	void startGame();
	
	///This says whether the game is ready to start
	bool isGameReadyToStart();
	
	///This is intended to add an AI to the game
	void addAIPlayer(AI::ImplementitionID type);

	///This kicks/removes a player from the game
	void kickPlayer(int playerNum);
	
	///This updates the team for a player
	void changeTeam(int playerNum, int teamNum);
	
	///Sends a message to other players in the game
//	void sendMessage(const std::string& message);
	
	///Returns the reason for being kicked
	YOGKickReason getKickReason() const;
	
	///Adds an event listener
	void addEventListener(MultiplayerGameEventListener* listener);
	
	///Removes an event listener
	void removeEventListener(MultiplayerGameEventListener* listener);
	
	///Returns the player number of the local player
	int getLocalPlayerNumber();

	///Gets the username of the local player
	std::string getUsername() const;

	///Gets the chat channel for this game
	Uint32 getChatChannel() const;
	
protected:
	friend class YOGClient;
	MultiplayerGamePlayerManager playerManager;

	///This receives a message that is sent to the game
	void recieveMessage(boost::shared_ptr<NetMessage> message);
	
	///This will start the game
	void startEngine();
	
	///Sets the default values for latency and order frame rate in the game header for a YOG game
	void setDefaultGameHeaderValues();
	
	///Sends the event to all listeners
	void sendToListeners(boost::shared_ptr<MultiplayerGameEvent> event);
	
	int getLocalPlayer();
private:
	boost::shared_ptr<YOGClient> client;
	GameJoinCreationState gjcState;
	YOGGameCreateRefusalReason creationState;
	YOGGameJoinRefusalReason joinState;
	YOGKickReason kickReason;
	MapHeader mapHeader;
	GameHeader gameHeader;
	NetEngine* netEngine;
	boost::shared_ptr<MapAssembler> assembler;
	bool haveMapHeader;
	bool haveGameHeader;
	bool wasReadyToStart;
	bool sentReadyToStart;
	std::list<MultiplayerGameEventListener*> listeners;
	Uint32 chatChannel;
};


#endif
