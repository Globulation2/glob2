// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGServerPlayer_h
#define __YOGServerPlayer_h

#include <memory>
#include <list>
#include "NetConnection.h"
#include "YOGConsts.h"
#include "YOGGameInfo.h"
#include "YOGPlayerSessionInfo.h"


class YOGServer;
class YOGServerGame;
class NetMessage;
class P2PManager;

using std::weak_ptr;
///This represents a connected user on the YOG server.
class YOGServerPlayer
{
public:
	///Establishes a YOGServerPlayer on the given connection.
	YOGServerPlayer(shared_ptr<NetConnection> connection, Uint16 id, YOGServer& server);

	///Updates the YOGServerPlayer. This deals with all incoming messages.
	void update();

	///Returns true if this YOGServerPlayer is still connected
	bool isConnected();

	///Sends a message to the player. Caution should be taken
	///that the client code knows how to handle this message
	///type.
	void sendMessage(shared_ptr<NetMessage> message);

	///Sets the player ID for this connection
	void setPlayerID(Uint16 id);

	///Returns the ID for this player
	Uint16 getPlayerID();
	
	///Returns the game id
	Uint16 getGameID();

	///Returns the name of the player, or blank if they haven't logged in
	std::string getPlayerName();

	///Returns the ip address of the player
	std::string getPlayerIP();

	///Returns the game the player is connected to
	std::shared_ptr<YOGServerGame> getGame();

	///Returns the players ping such that, statistically, 99.7% of all pings from this client
	///would be under this amount, so long as pings are normally distributed, which I've
	///found that they are
	unsigned getAveragePing() const;
	
	///This returns the port for the p2p connection client end on this player
	int getP2PPort();
	
	///Tells this YOGServerPlayer to close connection
	void closeConnection();
private:
	///This enum represents the state machine of the initial connection
	enum ConnectionState
	{
		///Means this is waiting for the client to send version information to the server.
		WaitingForClientInformation,
		///Server information, such as the IRC server and server policies, needs to be sent
		NeedToSendServerInformation,
		///Means its waiting for a login attempt by the client.
		WaitingForLoginAttempt,
		///A login accceptance needs to be sent
		NeedToSendLoginAccepted,
		///A login refusal needs to be sent
		NeedToSendLoginRefusal,
		///A registration acceptance needs to be sent
		NeedToSendRegistrationAccepted,
		///A registration acceptance needs to be sent
		NeedToSendRegistrationRefused,
		///This means the user is on standby, 
		ClientOnStandby,
	};

	enum GameListState
	{
		///Game list information needs to be sent
		UpdatingGameList,
		///Nothing needs to be sent
		GameListWaiting,
	};
	
	enum PlayerListState
	{
		///Player list information needs to be sent
		UpdatingPlayerList,
		///Nothing needs to be sent yet
		PlayerListWaiting,
	};

	ConnectionState connectionState;
	GameListState gameListState;
	PlayerListState playerListState;

	shared_ptr<NetConnection> connection;
	YOGServer& server;
	Uint16 netVersion;
	YOGLoginState loginState;

	///Send outgoing messsages involving ConnectionState
	void updateConnectionSates();

	///Send outgoing messages involving the game and player lists
	void updateGamePlayerLists();

	///Handles a request to create a new game
	void handleCreateGame(const std::string& gameName);

	///Handles a request to join a game
	void handleJoinGame(Uint16 gameID);
	
	///Stores a copy of the games that the player knows about, bassically
	///the list as it was on the last game list update
	std::list<YOGGameInfo> playersGames;
	///Stores a copy of the players that the player knows about.
	///This is a synchronized list of what the client has
	std::list<YOGPlayerSessionInfo> playersPlayerList;
	///The playerID, used to identify the assocciatted YOGPlayerSessionInfo
	Uint16 playerID;
	///the name of the player after logging in
	std::string playerName;
	///This is the local p2p port that the player is using for incoming p2p connections
	int port;	
	
	///Tells what game the player is currently a part of
	Uint16 gameID;
	///Links to the connected game
	weak_ptr<YOGServerGame> game;

	///Counts down between sending a ping
	Uint64 pingCountdown;
	///This says the time when the ping was sent, 0 means not waiting on ping reply
	Uint64 pingSendTime;
	///This holds the most recent 5 pings
	std::list<Uint64> pings;
	
};





#endif
