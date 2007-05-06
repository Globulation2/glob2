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
		shared_ptr<NetSendClientInformation> message = new NetSendClientInformation;
		nc.sendMessage(message);
		connectionState == WaitingForServerInformation;
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
	///This recieves a game list update message
	if(type==MNetUpdateGameList)
	{
		shared_ptr<NetUpdateGameList> info = static_pointer_cast<NetUpdateGameList>(message);
		info->applyDifferences(games);
		connectionState = ClientOnStandby;
	}
}



ConnectionState YOGClient::getConnectionState() const
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
	shared_ptr<NetAttemptLogin> message = new NetAttemptLogin(username, password);
	nc.sendMessage(message);
	connectionState = WaitingForLoginReply;
}



YOGLoginState YOGClient::getLoginState() const
{
	return loginState;
}



const std::list<YOGGameInfo>& YOGClient::getGameList() const
{
	return games;
}



void YOGClient::requestGameListUpdate()
{
	//unimplemented
}


