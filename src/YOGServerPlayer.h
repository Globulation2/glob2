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

#ifndef __YOGServerPlayer_h
#define __YOGServerPlayer_h

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <list>
#include "NetConnection.h"
#include "YOGConsts.h"
#include "YOGGameInfo.h"
#include "YOGPlayerInfo.h"

using namespace boost;

class YOGServer;
class YOGServerGame;
class NetMessage;

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

	///Returns the game the player is connected to
	boost::shared_ptr<YOGServerGame> getGame();

	///Returns the players current average ping
	unsigned getAveragePing() const;
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
	std::list<YOGPlayerInfo> playersPlayerList;
	///The playerID, used to identify the assocciatted YOGPlayerInfo
	Uint16 playerID;
	///the name of the player after logging in
	std::string playerName;

	///Tells what game the player is currently a part of
	Uint16 gameID;
	///Links to the connected game
	weak_ptr<YOGServerGame> game;

	///Counts down between sending a ping
	unsigned short pingCountdown;
	///This tells the current average value of the pings
	unsigned pingValue;
	///This says the time when the ping was sent, 0 means not waiting on ping reply
	unsigned pingSendTime;
	///This holds the most recent 5 pings
	std::list<unsigned> pings;
	
};





#endif
