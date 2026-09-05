// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "YOGClient.h"
#include "MapHeader.h"
#include "GameHeader.h"
#include "NetEngine.h"
#include "MultiplayerGameEventListener.h"
#include <list>
#include "NetGamePlayerManager.h"
#include "NetReteamingInformation.h"

// === Multiplayer-game sentinels and tunables ===

//! "Local player not found in this game header" sentinel returned by
//! MultiplayerGame::getLocalPlayer(). See MultiplayerGame.cpp:696.
static constexpr int LOCAL_PLAYER_NONE = -1;

//! "Download percentage never reported yet" sentinel for
//! MultiplayerGame::previousPercentage. Distinct from 100% (= complete);
//! valid percentages are 0..100, so 255 is unambiguous.
//! See MultiplayerGame.cpp:43.
static constexpr Uint8 MP_DOWNLOAD_PCT_UNREPORTED = 255;

//! Download "complete" percentage threshold. Used both as the equality
//! test for download-finished (MultiplayerGame.cpp:311) and as the
//! "no transfer in progress / done" return value of
//! MultiplayerGame::percentageDownloadFinished()
//! (MultiplayerGame.cpp:715, 719).
static constexpr Uint8 DOWNLOAD_PCT_COMPLETE = 100;

//! Default network latency, in tick slots, written into the GameHeader
//! by MultiplayerGame::setDefaultGameHeaderValues().
//! See MultiplayerGame.cpp:655.
static constexpr int MP_DEFAULT_GAME_LATENCY_TICKS = 12;

//! Default order send-rate (1 send per N ticks) written into the
//! GameHeader by MultiplayerGame::setDefaultGameHeaderValues().
//! See MultiplayerGame.cpp:656.
static constexpr int MP_DEFAULT_ORDER_RATE_TICKS = 6;

//! "No chat channel selected yet" sentinel for YOGClientChatChannel
//! channel id (Uint32). See MultiplayerGameScreen.cpp:29.
static constexpr Uint32 YOG_CHAT_CHANNEL_NONE = static_cast<Uint32>(-1);

///This class represents a multi-player game, both in the game and while waiting for players
///and setting up options. It channels its information through a YOGClient
class MultiplayerGame
{
public:
	///Creates a game instance and links it with the provided YOGClient
	MultiplayerGame(std::shared_ptr<YOGClient> client);
	
	~MultiplayerGame();
	
	///Should be called frequently
	void update();
	
	///Attempt to create a new game on the server with the given name, and wait for reply
	void createNewGame(const std::string& name);
	
	///Attempt to join an existing game on the server with the given id, 
	void joinGame(Uint16 gameID);

	///Leaves the game you currently occupy
	void leaveGame();

	///This is the mode
	enum MultiplayerMode
	{
		NoMode,
		JoinedGame,
		HostingGame	
	};

	///Represents the current step the system is on with regards to joining or creating a game
	enum GameJoinCreationState
	{
		NothingYet,
		WaitingForInitialReply,
		WaitingForGameInformation,
		SendingGameInformation,
		ConnectingToGameRouter,
		ReadyToGo,
	};
	
	///This returns the current mode
	MultiplayerMode getMultiplayerMode() const;
	
	///Returns the current state of joining or creating a game
	GameJoinCreationState getGameJoinCreationState() const;
	
	///Returns the reason the creation of a game was refused
	YOGServerGameCreateRefusalReason getGameCreationState();

	///Returns the reason the joining of a game was refused
	YOGServerGameJoinRefusalReason getGameJoinState();

	///Sets the map header for this game, only for when hosting a game
	void setMapHeader(MapHeader& mapHeader);

	///Returns the map header of this game
	MapHeader& getMapHeader();

	///Returns the game header. It can be modified. After modifying it,
	///one must call updateGameHeader(). At no point should any changes
	///be done to the base players, they are managed by the server
	GameHeader& getGameHeader();
	
	///Call this to send the the changes of the game header to the server
	void updateGameHeader();
	
	///Call this to send the the player-changes to the server
	void updatePlayerChanges();
	
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
	bool isReadyToStart(YOGPlayerID playerID);
	
	///Sets whether the player (as in the actual person) is ready, usually by clicking a check box
	void setHumanReady(bool isReady);
	
	///This is true if the map and game headers have been recieved and the game is connected to the game router
	bool isFullyInGame();
protected:
	friend class YOGClient;

	///This receives a message that is sent to the game
	void recieveMessage(std::shared_ptr<NetMessage> message);
	
	///This will start the game
	void startEngine();
	
	///Sets the default values for latency and order frame rate in the game header for a YOG game
	void setDefaultGameHeaderValues();
	
	///Sends the event to all listeners
	void sendToListeners(std::shared_ptr<MultiplayerGameEvent> event);
	
	///Puts together reteaming information from the game header in the file
	NetReteamingInformation constructReteamingInformation(const std::string& file);
	
	int getLocalPlayer();
private:
	std::shared_ptr<YOGClient> client;
	
	//These are various states of the system
	MultiplayerMode mode;
	GameJoinCreationState state;
	YOGServerGameCreateRefusalReason creationState;
	YOGServerGameJoinRefusalReason joinState;
	YOGKickReason kickReason;
	
	//The id of the game, the id of the file (for transfering the map to clients), the ip of the router, and the id of the chat channel
	Uint16 gameID;
	Uint16 fileID;
	Uint32 chatChannel;
	std::string gameRouterIP;
	
	//This is information about the game
	GameHeader gameHeader;
	MapHeader mapHeader;
	
	//This is for if whether the player is ready to start or not (all factors considered like all information recieved,
	//map downloaded if need be etc..)
	bool wasReadyToStart;
	bool sentReadyToStart;
	bool humanReadyToStart;
	
	//Miscalaneous
	bool isStarting;
	bool needToSendMapHeader;
	Uint8 previousPercentage;
	Uint8 numberOfConnectionAttempts;

	//API/engine stuff
	NetEngine* netEngine;
	NetGamePlayerManager playerManager;
	std::list<MultiplayerGameEventListener*> listeners;
};

