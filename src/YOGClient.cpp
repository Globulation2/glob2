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

#include "YOGClient.h"
#include <iostream>
#include "MultiplayerGame.h"
#include "MapAssembler.h"
#include "YOGGameListManager.h"
#include "YOGPlayerListManager.h"

YOGClient::YOGClient(const std::string& server)
{
	connect(server);
	
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
	listener=NULL;
	wasConnected=false;
	
	//By default, the client creates its own game list manager and player list manager
	gameListManager.reset(new YOGGameListManager(this));
	playerListManager.reset(new YOGPlayerListManager(this));
}



void YOGClient::connect(const std::string& server)
{
	initialize();
	nc.openConnection(server, YOG_SERVER_PORT);
	connectionState = NeedToSendClientInformation;
	if(nc.isConnected())
		wasConnected=true;
}



bool YOGClient::isConnected()
{
	return nc.isConnected();
}



bool YOGClient::isConnecting()
{
	return nc.isConnecting();
}



void YOGClient::update()
{
	nc.update();

	if(server)
		server->update();

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
			if(gameListManager)
				gameListManager->recieveMessage(message);
		}
		///This recieves a player list update message
		if(type==MNetUpdatePlayerList)
		{
			if(playerListManager)
				playerListManager->recieveMessage(message);
		}
		///This recieves a YOGMessage list update message
		if(type==MNetSendYOGMessage)
		{
			shared_ptr<NetSendYOGMessage> yogmessage = static_pointer_cast<NetSendYOGMessage>(message);
			if(chatChannels.find(yogmessage->getChannel()) != chatChannels.end())
			{
				chatChannels[yogmessage->getChannel()]->recieveMessage(yogmessage->getMessage());
			}
			else
			{
				std::cerr<<"Recieved YOGMessage on a channel without a local YOGChatChannel"<<std::endl;
			}
		}

		if(type==MNetCreateGameAccepted)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetCreateGameRefused)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetGameJoinAccepted)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetGameJoinRefused)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSendMapHeader)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSendGameHeader)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSendGamePlayerInfo)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetStartGame)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetRefuseGameStart)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSendOrder)
		{
			//ignore orders for when there is no joined game,
			//say, the leftover orders in transit after a player
			//quits a game
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetRequestMap)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetKickPlayer)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetReadyToLaunch)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetEveryoneReadyToLaunch)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetNotEveryoneReadyToLaunch)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSetLatencyMode)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type == MNetPlayerJoinsGame)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type == MNetAddAI)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type == MNetRemoveAI)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type == MNetChangePlayersTeam)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSendFileInformation)
		{
			if(assembler)
				assembler->handleMessage(message);
		}
		if(type==MNetRequestNextChunk)
		{
			if(assembler)
				assembler->handleMessage(message);
		}
		if(type==MNetSendFileChunk)
		{
			if(assembler)
				assembler->handleMessage(message);
		}
		if(type == MNetPing)
		{
			shared_ptr<NetPingReply> event(new NetPingReply);
			nc.sendMessage(event);
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


void YOGClient::attemptRegistration(const std::string& nusername, const std::string& password)
{
	username = nusername;
	shared_ptr<NetAttemptRegistration> message(new NetAttemptRegistration(username, password));
	nc.sendMessage(message);
	connectionState = WaitingForRegistrationReply;
}


YOGLoginState YOGClient::getLoginState() const
{
	return loginState;
}



void YOGClient::disconnect()
{
	shared_ptr<NetDisconnect> message(new NetDisconnect);
	nc.sendMessage(message);
	nc.closeConnection();
	connectionState = NotConnected;
	wasConnected=false;
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



void YOGClient::addYOGChatChannel(YOGChatChannel* channel)
{
	chatChannels[channel->getChannelID()] = channel;
}



void YOGClient::removeYOGChatChannel(YOGChatChannel* channel)
{
	chatChannels.erase(channel->getChannelID());
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



void YOGClient::attachGameServer(boost::shared_ptr<YOGGameServer> nserver)
{
	server = nserver;
}



void  YOGClient::setP2PConnection(boost::shared_ptr<P2PConnection> connection)
{
	p2pconnection = connection;
}



void YOGClient::setGameListManager(boost::shared_ptr<YOGGameListManager> ngameListManager)
{
	gameListManager = ngameListManager;
}



boost::shared_ptr<YOGGameListManager> YOGClient::getGameListManager()
{
	return gameListManager;
}



void YOGClient::setPlayerListManager(boost::shared_ptr<YOGPlayerListManager> nplayerListManager)
{
	playerListManager = nplayerListManager;
}



boost::shared_ptr<YOGPlayerListManager> YOGClient::getPlayerListManager()
{
	return playerListManager;
}



