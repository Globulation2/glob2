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

#ifndef __YOGPlayer_h
#define __YOGPlayer_h

#include <boost/shared_ptr.hpp>
#include <list>
#include "NetConnection.h"
#include "YOGConsts.h"

using namespace boost;

class YOGGameServer;

///This represents a connected user on the YOG server.
class YOGPlayer
{
public:
	///Establishes a YOGPlayer on the given connection.
	YOGPlayer(shared_ptr<NetConnection> connection);

	///Updates the YOGPlayer. This deals with all incoming messages.
	void update(YOGGameServer& server);

	///Returns true if this YOGPlayer is still connected
	bool isConnected();

	///Sends a message to the player. Caution should be taken
	///that the client code knows how to handle this message
	///type.
	void sendMessage(shared_ptr<NetMessage> message);

	///Sets the player ID for this connection
	void setPlayerID(Uint16 id);

	///Returns the ID for this player
	Uint16 getPlayerID();

private:
	///This enum represents the state machine of a connection.
	enum PlayerState
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
		///This means the user is on standby, 
		ClientOnStandby,
	};

	enum GameListState
	{
		///Game list information needs to be sent
		NeedToSendGameList,
		///Nothing needs to be sent
		GameListNormal,
	};
	
	enum PlayerListState
	{
		///Player list information needs to be sent
		NeedToSendPlayerList,
		///Nothing needs to be sent
		PlayerListNormal,
	};

	PlayerState connectionState;
	GameListState gameListState;
	PlayerListState playerListState;

	shared_ptr<NetConnection> connection;
	Uint16 versionMinor;
	YOGLoginState loginState;
	
	///Stores a copy of the games that the player knows about, bassically
	///the list as it was on the last game list update
	std::list<YOGGameInfo> playersGames;
	///Stores a copy of the players that the player knows about.
	///This is a synchronized list of what the client has
	std::list<YOGPlayerInfo> playersPlayerList;
	///The playerID, used to identify the assocciatted YOGPlayerInfo
	Uint16 playerID;
};





#endif
