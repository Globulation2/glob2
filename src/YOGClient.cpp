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

#include "YOGClient.h"



YOGClient::YOGClient(const std::string& server)
{
	connectionState = NotConnected;
	loginPolicy = YOGUnknownLoginPolicy;
	gamePolicy = YOGUnknownGamePolicy;
	loginState = YOGLoginUnknown;
	gameListChanged = false;
	playerListChanged = false;
	connect(server);
}



YOGClient::YOGClient()
{
	
}



void YOGClient::connect(const std::string& server)
{
	nc.openConnection(server, YOG_SERVER_PORT);
	connectionState = NeedToSendClientInformation;
}



void YOGClient::update()
{
	//If we need to send client information, send it
	if(connectionState == NeedToSendClientInformation)
	{
		shared_ptr<NetSendClientInformation> message(new NetSendClientInformation);
		nc.sendMessage(message);
		connectionState = WaitingForServerInformation;
	}


	//Parse incoming messages.
	shared_ptr<NetMessage> message = nc.getMessage();
	Uint8 type = message->getMessageType();
	//This recieves the server information
	if(type==MNetSendServerInformation)
	{
		shared_ptr<NetSendServerInformation> info = static_pointer_cast<NetSendServerInformation>(message);
		loginPolicy = info->getLoginPolicy();
		gamePolicy = info->getGamePolicy();
		connectionState = WaitingForLoginInformation;
	}
	//This recieves a login acceptance message
	if(type==MNetLoginSuccessful)
	{
		shared_ptr<NetLoginSuccessful> info = static_pointer_cast<NetLoginSuccessful>(message);
		connectionState = WaitingForGameList;
		loginState = YOGLoginSuccessful;
	}
	//This recieves a login refusal message
	if(type==MNetRefuseLogin)
	{
		shared_ptr<NetRefuseLogin> info = static_pointer_cast<NetRefuseLogin>(message);
		connectionState = WaitingForLoginInformation;
		loginState = info->getRefusalReason();
	}
	//This recieves a registration acceptance message
	if(type==MNetAcceptRegistration)
	{
		shared_ptr<NetAcceptRegistration> info = static_pointer_cast<NetAcceptRegistration>(message);
		connectionState = WaitingForGameList;
		loginState = YOGLoginSuccessful;
	}
	//This recieves a regisration refusal message
	if(type==MNetRefuseRegistration)
	{
		shared_ptr<NetRefuseRegistration> info = static_pointer_cast<NetRefuseRegistration>(message);
		connectionState = WaitingForLoginInformation;
		loginState = info->getRefusalReason();
	}
	///This recieves a game list update message
	if(type==MNetUpdateGameList)
	{
		shared_ptr<NetUpdateGameList> info = static_pointer_cast<NetUpdateGameList>(message);
		info->applyDifferences(games);
		connectionState = ClientOnStandby;
		gameListChanged=true;
	}
	///This recieves a player list update message
	if(type==MNetUpdatePlayerList)
	{
		shared_ptr<NetUpdatePlayerList> info = static_pointer_cast<NetUpdatePlayerList>(message);
		info->applyDifferences(players);
		connectionState = ClientOnStandby;
		playerListChanged=true;
	}
}



YOGClient::ConnectionState YOGClient::getConnectionState() const
{
	return connectionState;
}



YOGLoginPolicy YOGClient::getLoginPolicy() const
{
	return loginPolicy;
}



YOGGamePolicy YOGClient::getGamePolicy() const
{
	return gamePolicy;
}



void YOGClient::attemptLogin(const std::string& username, const std::string& password)
{
	shared_ptr<NetAttemptLogin> message(new NetAttemptLogin(username, password));
	nc.sendMessage(message);
	connectionState = WaitingForLoginReply;
}


void YOGClient::attemptRegistration(const std::string& username, const std::string& password)
{
	shared_ptr<NetAttemptRegistration> message(new NetAttemptRegistration(username, password));
	nc.sendMessage(message);
	connectionState = WaitingForRegistrationReply;
}


YOGLoginState YOGClient::getLoginState() const
{
	return loginState;
}



const std::list<YOGGameInfo>& YOGClient::getGameList() const
{
	return games;
}



const std::list<YOGPlayerInfo>& YOGClient::getPlayerList() const
{
	return players;
}



void YOGClient::requestGameListUpdate()
{
	//unimplemented
}



void YOGClient::requestPlayerListUpdate()
{
	//unimplemented
}



bool YOGClient::hasGameListChanged()
{
	if(gameListChanged)
	{
		gameListChanged = false;
		return true;
	}
}


bool YOGClient::hasPlayerListChanged()
{
	if(playerListChanged)
	{
		playerListChanged = false;
		return true;
	}
}



void YOGClient::disconnect()
{
	shared_ptr<NetDisconnect> message(new NetDisconnect);
	nc.sendMessage(message);
	nc.closeConnection();
	connectionState = NotConnected;
}


void YOGClient::removeGame()
{
	//unimplemented
}



void YOGClient::gameHasStarted()
{
	//unimplemented
}



void YOGClient::gameHasFinished()
{
	//unimplemented
}



void YOGClient::sendMessage(boost::shared_ptr<YOGMessage> message)
{
	//unimplemented
}


boost::shared_ptr<YOGMessage> YOGClient::getMessage()
{
	//unimplemented
	return boost::shared_ptr<YOGMessage>();
}
