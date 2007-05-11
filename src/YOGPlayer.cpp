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

#include "YOGPlayer.h"

YOGPlayer::YOGPlayer(shared_ptr<NetConnection> connection) : connection(connection)
{
	connectionState = WaitingForClientInformation;
	refusalReason = YOGLoginSuccessful;
}



void YOGPlayer::update(YOGGameServer& server)
{
	//Parse incoming messages.
	shared_ptr<NetMessage> message = nc.getMessage();
	Uint8 type = message->getMessageType();
	//This recieves the client information
	if(type==MNetSendClientInformation)
	{
		shared_ptr<NetSendClientInformation> info = static_pointer_cast<NetSendClientInformation>(message);
		versionMinor = info->getVersionMinor();
		connectionState = NeedToSendServerInformation;
	}
	//This recieves a login attempt
	else if(type==MNetAttemptLogin)
	{
		shared_ptr<NetAttemptLogin> info = static_pointer_cast<NetAttemptLogin>(message);
		std::string username = info->getUsername();
		std::string password = info->getPassword();
		loginState = server.verifyLoginInformation(username, password);
		if(refusalReason == YOGLoginSuccessful)
		{
			connectionState = NeedToSendLoginAccepted;
		}
		else
		{
			connectionState = NeedToSendLoginRefusal;
		}
	}


	//Send outgoing messages
	//Send the server information
	if(connectionState==NeedToSendServerInformation)
	{
		shared_ptr<NetSendServerInformation> info = new NetSendServerInformation(server.getLoginPolicy(), server.getGamePolicy());
		connection->sendMessage(info);
		connectionState = WaitingForLoginAttempt;
	}
	//Send the login accepted message
	if(connectionState==NeedToSendLoginAccepted)
	{
		shared_ptr<NetLoginSuccessful> accepted = new NetLoginSuccessful;
		connection->sendMessage(accepted);
		connectionState = NeedToSendGameList;
	}
	//Send the login refused message
	if(connectionState==NeedToSendLoginRefusal)
	{
		shared_ptr<NetRefuseLogin> refused = new NetRefuseLogin(loginState);
		connection->sendMessage(refused);
		connectionState = WaitingForLoginAttempt;
	}
	//Send an updated game list to the user
	if(connectionState==NeedToSendGameList)
	{
		shared_ptr<NetUpdateGameList> gamelist = new NetUpdateGameList;
		gamelist->updateDifferences(server.getGameList(), playersGames);
		playersGames = server.getGameList();
		connection->sendMessage(gamelist);
		connectionState = NeedToSendPlayerList;
	}
	//Send an updated player list to the user
	if(connectionState==NeedToSendPlayerList)
	{
		shared_ptr<NetUpdatePlayerList> playerlist = new NetUpdatePlayerList;
		gamelistplayerlistupdateDifferences(server.getPlayerList(), playersPlayerList);
		playersPlayerList = server.getGameList();
		connection->sendMessage(playerlist);
		connectionState = ClientOnStandby;
	}
}


bool YOGPlayer::isConnected()
{
	return connection.isConnected();
}



void YOGPlayer::sendMessage(shared_ptr<NetMessage> message)
{
	connection->sendMessage(message);
}


