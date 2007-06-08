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
#include <iostream>
#include "MultiplayerGame.h"
#include "MapAssembler.h"

YOGClient::YOGClient(const std::string& server)
{
	connect(server);
	listener=NULL;
	wasConnected=false;
}



YOGClient::YOGClient()
{
	initialize();
}



void YOGClient::initialize()
{
	connectionState = NotConnected;
	loginPolicy = YOGUnknownLoginPolicy;
	gamePolicy = YOGUnknownGamePolicy;
	loginState = YOGLoginUnknown;
	playerID=0;
}



void YOGClient::connect(const std::string& server)
{
	initialize();
	nc.openConnection(server, YOG_SERVER_PORT);
	connectionState = NeedToSendClientInformation;
	wasConnected=true;
}



bool YOGClient::isConnected()
{
	return nc.isConnected();
}



void YOGClient::update()
{
	if(!nc.isConnected() && wasConnected)
	{
		if(listener)
		{
			shared_ptr<YOGConnectionLostEvent> event(new YOGConnectionLostEvent);
			listener->handleYOGEvent(event);
		}
		wasConnected=false;
	}

	if(nc.isConnected())
	{
		//If we need to send client information, send it
		if(connectionState == NeedToSendClientInformation)
		{
			shared_ptr<NetSendClientInformation> message(new NetSendClientInformation);
			nc.sendMessage(message);
			connectionState = WaitingForServerInformation;
		}

		//Parse incoming messages and generate events
		shared_ptr<NetMessage> message = nc.getMessage();
		if(!message)
			return;
		Uint8 type = message->getMessageType();
		//This recieves the server information
		if(type==MNetSendServerInformation)
		{
			shared_ptr<NetSendServerInformation> info = static_pointer_cast<NetSendServerInformation>(message);
			loginPolicy = info->getLoginPolicy();
			gamePolicy = info->getGamePolicy();
			playerID = info->getPlayerID();
			if(listener)
			{
				shared_ptr<YOGConnectedEvent> event(new YOGConnectedEvent);
				listener->handleYOGEvent(event);
			}
			connectionState = WaitingForLoginInformation;
		}
		//This recieves a login acceptance message
		if(type==MNetLoginSuccessful)
		{
			shared_ptr<NetLoginSuccessful> info = static_pointer_cast<NetLoginSuccessful>(message);
			connectionState = ClientOnStandby;
			loginState = YOGLoginSuccessful;
			if(listener)
			{
				shared_ptr<YOGLoginAcceptedEvent> event(new YOGLoginAcceptedEvent);
				listener->handleYOGEvent(event);
			}
		}
		//This recieves a login refusal message
		if(type==MNetRefuseLogin)
		{
			shared_ptr<NetRefuseLogin> info = static_pointer_cast<NetRefuseLogin>(message);
			connectionState = WaitingForLoginInformation;
			loginState = info->getRefusalReason();
			if(listener)
			{
				shared_ptr<YOGLoginRefusedEvent> event(new YOGLoginRefusedEvent(info->getRefusalReason()));
				listener->handleYOGEvent(event);
			}
		}
		//This recieves a registration acceptance message
		if(type==MNetAcceptRegistration)
		{
			shared_ptr<NetAcceptRegistration> info = static_pointer_cast<NetAcceptRegistration>(message);
			connectionState = ClientOnStandby;
			loginState = YOGLoginSuccessful;
			if(listener)
			{
				shared_ptr<YOGLoginAcceptedEvent> event(new YOGLoginAcceptedEvent);
				listener->handleYOGEvent(event);
			}
		}
		//This recieves a regisration refusal message
		if(type==MNetRefuseRegistration)
		{
			shared_ptr<NetRefuseRegistration> info = static_pointer_cast<NetRefuseRegistration>(message);
			connectionState = WaitingForLoginInformation;
			loginState = info->getRefusalReason();
			if(listener)
			{
				shared_ptr<YOGLoginRefusedEvent> event(new YOGLoginRefusedEvent(info->getRefusalReason()));
				listener->handleYOGEvent(event);
			}
		}
		///This recieves a game list update message
		if(type==MNetUpdateGameList)
		{
			shared_ptr<NetUpdateGameList> info = static_pointer_cast<NetUpdateGameList>(message);
			info->applyDifferences(games);
			if(listener)
			{
				shared_ptr<YOGGameListUpdatedEvent> event(new YOGGameListUpdatedEvent);
				listener->handleYOGEvent(event);
			}
		}
		///This recieves a player list update message
		if(type==MNetUpdatePlayerList)
		{
			shared_ptr<NetUpdatePlayerList> info = static_pointer_cast<NetUpdatePlayerList>(message);
			info->applyDifferences(players);
			if(listener)
			{
				shared_ptr<YOGPlayerListUpdatedEvent> event(new YOGPlayerListUpdatedEvent);
				listener->handleYOGEvent(event);
			}
		}
		///This recieves a YOGMessage list update message
		if(type==MNetSendYOGMessage)
		{
			shared_ptr<NetSendYOGMessage> yogmessage = static_pointer_cast<NetSendYOGMessage>(message);
			messages.push(yogmessage->getMessage());
		}

		if(type==MNetCreateGameAccepted)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetCreateGameRefused)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetGameJoinAccepted)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetGameJoinRefused)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetSendMapHeader)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetSendGameHeader)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetPlayerJoinsGame)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetPlayerLeavesGame)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetStartGame)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetSendOrder)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetRequestMap)
		{
			joinedGame->recieveMessage(message);
		}
		if(type==MNetKickPlayer)
		{
			joinedGame->recieveMessage(message);
		}


		if(type==MNetSendFileInformation)
		{
			assembler->handleMessage(message);
		}
		if(type==MNetRequestNextChunk)
		{
			assembler->handleMessage(message);
		}
		if(type==MNetSendFileChunk)
		{
			assembler->handleMessage(message);
		}
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



Uint16 YOGClient::getPlayerID() const
{
	return playerID;
}



void YOGClient::attemptLogin(const std::string& nusername, const std::string& password)
{
	username = nusername;
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



std::string YOGClient::findPlayerName(Uint16 playerID)
{
	for(std::list<YOGPlayerInfo>::iterator i = players.begin(); i != players.end(); ++i)
	{
		if(i->getPlayerID() == playerID)
			return i->getPlayerName();
	}
	return "";
}



void YOGClient::requestGameListUpdate()
{
	//unimplemented
}



void YOGClient::requestPlayerListUpdate()
{
	//unimplemented
}



void YOGClient::disconnect()
{
	shared_ptr<NetDisconnect> message(new NetDisconnect);
	nc.sendMessage(message);
	nc.closeConnection();
	connectionState = NotConnected;
	wasConnected=false;
}



void YOGClient::sendMessage(boost::shared_ptr<YOGMessage> mmessage)
{
	messages.push(mmessage);

	shared_ptr<NetSendYOGMessage> message(new NetSendYOGMessage(mmessage));
	nc.sendMessage(message);
}


boost::shared_ptr<YOGMessage> YOGClient::getMessage()
{
	if(messages.size())
	{
		boost::shared_ptr<YOGMessage> m = messages.front();
		messages.pop();
		return m;
	}
	return boost::shared_ptr<YOGMessage>();
}



std::string YOGClient::getUsername() const
{
	return username;
}



void YOGClient::createGame(const std::string& name)
{
	shared_ptr<NetCreateGame> message(new NetCreateGame(name));
	nc.sendMessage(message);
}



void YOGClient::setMultiplayerGame(boost::shared_ptr<MultiplayerGame> game)
{
	joinedGame=game;
}



boost::shared_ptr<MultiplayerGame> YOGClient::getMultiplayerGame()
{
	return joinedGame;
}



void YOGClient::sendNetMessage(boost::shared_ptr<NetMessage> message)
{
    nc.sendMessage(message);
}



void YOGClient::setMapAssembler(boost::shared_ptr<MapAssembler> nassembler)
{
	assembler=nassembler;
}



boost::shared_ptr<MapAssembler> YOGClient::getMapAssembler()
{
	return assembler;
}



void YOGClient::setEventListener(YOGEventListener* nlistener)
{
	listener=nlistener;
}
