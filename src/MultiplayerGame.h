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
#include <list>
#include "NetGamePlayerManager.h"
#include "NetReteamingInformation.h"

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
	YOGServerGameCreateRefusalReason getGameCreationState();

	///Returns the reason the joining of a game was refused
	YOGServerGameJoinRefusalReason getGameJoinState();

	///Sets the map header
	void setMapHeader(MapHeader& mapHeader);

	///Returns the map header
	MapHeader& getMapHeader();
	
	///Tells whether the game is still connected to the server
	bool isStillConnected() const;

	///Returns the game header. It can be modified. After modifying it,
	///one must call updateGameHeader(). At no point should any changes
	///be done to the base players, they are managed by the server
	GameHeader& getGameHeader();
	
	///Call this to send the the changes of the game header to the server
	void updateGameHeader();
	
	///Call this to send the the player-changes to the server
	void updatePlayerChanges();
	
	///Tells whether the list of players has changed since the last call to this function
	bool hasPlayersChanged();
	
	///Sets the assocciatted net engine to push recieved orders into
	void setNetEngine(NetEngine* engine);
	
	///Causes the game to be started on all clients.
	void startGame();
	
	///This says whether the game is ready to start
	bool isGameReadyToStart();
	
	///This updates the local players ready state
	void updateReadyState();
	
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
	
	///Returns the percentage finished for the downloaded
	Uint8 percentageDownloadFinished();
	
	///Returns true if the MultiplayerGame is waiting for a reply from the server
	///to start the game
	bool isGameStarting();
	
	///This sets the game result for the local player
	void setGameResult(YOGGameResult result);
	
	///Returns true if the given player is ready to start
	bool isReadyToStart(int playerID);
	
	///Sets whether the player (as in the actual person) is ready, usually by clicking a check box
	void setHumanReady(bool isReady);
	
	///This is true if the map and game headers have been recieved and the game is connected to the game router
	bool isFullyInGame();
protected:
	friend class YOGClient;

	///This receives a message that is sent to the game
	void recieveMessage(boost::shared_ptr<NetMessage> message);
	
	///This will start the game
	void startEngine();
	
	///Sets the default values for latency and order frame rate in the game header for a YOG game
	void setDefaultGameHeaderValues();
	
	///Sends the event to all listeners
	void sendToListeners(boost::shared_ptr<MultiplayerGameEvent> event);
	
	///Puts together reteaming information from the game header in the file
	NetReteamingInformation constructReteamingInformation(const std::string& file);
	
	int getLocalPlayer();
private:
	boost::shared_ptr<YOGClient> client;
	GameJoinCreationState gjcState;
	YOGServerGameCreateRefusalReason creationState;
	YOGServerGameJoinRefusalReason joinState;
	YOGKickReason kickReason;
	MapHeader mapHeader;
	GameHeader gameHeader;
	NetEngine* netEngine;
	bool haveMapHeader;
	bool haveGameHeader;
	bool wasReadyToStart;
	bool sentReadyToStart;
	bool humanReady;
	std::list<MultiplayerGameEventListener*> listeners;
	Uint32 chatChannel;
	bool isStarting;
	Uint8 previousPercentage;
	Uint16 gameID;
	Uint16 fileID;
	bool wasConnectingToRouter;

	NetGamePlayerManager playerManager;
};


#endif
