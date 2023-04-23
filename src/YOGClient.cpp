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

#include <iostream>
#include "MultiplayerGame.h"
#include "NetMessage.h"
#include "YOGClientBlockedList.h"
#include "YOGClientChatChannel.h"
#include "YOGClientCommandManager.h"
#include "YOGClientDownloadableMapList.h"
#include "YOGClientEvent.h"
#include "YOGClientEventListener.h"
#include "YOGClientFileAssembler.h"
#include "YOGClientGameListManager.h"
#include "YOGClient.h"
#include "YOGClientMapUploader.h"
#include "YOGClientPlayerListManager.h"
#include "YOGClientRatedMapList.h"
#include "YOGMessage.h"
#include "YOGServer.h"

using boost::static_pointer_cast;
using boost::shared_ptr;

YOGClient::YOGClient(const std::string& server)
{
	initialize();
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
	wasConnected=false;
	wasConnecting=false;
	
	//By default, the client creates its own game list manager and player list manager
	gameListManager.reset(new YOGClientGameListManager(this));
	playerListManager.reset(new YOGClientPlayerListManager(this));
	commands.reset(new YOGClientCommandManager(this));
	downloadableMapList.reset(new YOGClientDownloadableMapList(this));
	uploader = NULL;
	downloader = NULL;
}



void YOGClient::connect(const std::string& server)
{
	initialize();
	nc.openConnection(server, YOG_SERVER_PORT);
	connectionState = NeedToSendClientInformation;
	wasConnecting=true;
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

	if(gameConnection)
		gameConnection->update();
	
	if(!nc.isConnecting() && wasConnecting)
	{
		if(nc.isConnected())
		{
			wasConnected = true;
			wasConnecting = false;
		}
		else
		{
			wasConnected = false;
			wasConnecting = false;
		}
	}

	if(!nc.isConnected() && wasConnected)
	{
		shared_ptr<YOGConnectionLostEvent> event(new YOGConnectionLostEvent);
		sendToListeners(event);
		wasConnected=false;
	}

	//If we need to send client information, send it
	if(connectionState == NeedToSendClientInformation)
	{
		shared_ptr<NetSendClientInformation> message(new NetSendClientInformation);
		nc.sendMessage(message);
		connectionState = WaitingForServerInformation;
	}

	//Parse incoming messages and generate events
	shared_ptr<NetMessage> message = nc.getMessage();
	while(message)
	{
		Uint8 type = message->getMessageType();
		//This recieves the server information
		if(type==MNetSendServerInformation)
		{
			shared_ptr<NetSendServerInformation> info = static_pointer_cast<NetSendServerInformation>(message);
			loginPolicy = info->getLoginPolicy();
			gamePolicy = info->getGamePolicy();
			playerID = info->getPlayerID();
			shared_ptr<YOGConnectedEvent> event(new YOGConnectedEvent);
			sendToListeners(event);
			connectionState = WaitingForLoginInformation;
		}
		//This recieves a login acceptance message
		if(type==MNetLoginSuccessful)
		{
			shared_ptr<NetLoginSuccessful> info = static_pointer_cast<NetLoginSuccessful>(message);
			connectionState = ClientOnStandby;
			loginState = YOGLoginSuccessful;
			ratedMapList = boost::shared_ptr<YOGClientRatedMapList>(new YOGClientRatedMapList(username));
			blocked = boost::shared_ptr<YOGClientBlockedList>(new YOGClientBlockedList(username));
			shared_ptr<YOGLoginAcceptedEvent> event(new YOGLoginAcceptedEvent);
			sendToListeners(event);
		}
		//This recieves a login refusal message
		if(type==MNetRefuseLogin)
		{
			shared_ptr<NetRefuseLogin> info = static_pointer_cast<NetRefuseLogin>(message);
			connectionState = WaitingForLoginInformation;
			loginState = info->getRefusalReason();
			shared_ptr<YOGLoginRefusedEvent> event(new YOGLoginRefusedEvent(info->getRefusalReason()));
			sendToListeners(event);
		}
		//This recieves a registration acceptance message
		if(type==MNetAcceptRegistration)
		{
			shared_ptr<NetAcceptRegistration> info = static_pointer_cast<NetAcceptRegistration>(message);
			connectionState = ClientOnStandby;
			loginState = YOGLoginSuccessful;
			ratedMapList = boost::shared_ptr<YOGClientRatedMapList>(new YOGClientRatedMapList(username));
			blocked = boost::shared_ptr<YOGClientBlockedList>(new YOGClientBlockedList(username));
			shared_ptr<YOGLoginAcceptedEvent> event(new YOGLoginAcceptedEvent);
			sendToListeners(event);
		}
		//This recieves a regisration refusal message
		if(type==MNetRefuseRegistration)
		{
			shared_ptr<NetRefuseRegistration> info = static_pointer_cast<NetRefuseRegistration>(message);
			connectionState = WaitingForLoginInformation;
			loginState = info->getRefusalReason();
			shared_ptr<YOGLoginRefusedEvent> event(new YOGLoginRefusedEvent(info->getRefusalReason()));
			sendToListeners(event);
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
				if(!blocked->isPlayerBlocked(yogmessage->getMessage()->getSender()))
				{
					chatChannels[yogmessage->getChannel()]->recieveMessage(yogmessage->getMessage());
				}
			}
			else
			{
				std::cerr<<"Recieved YOGMessage on a channel without a local YOGClientChatChannel"<<std::endl;
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
		if(type==MNetSendAfterJoinGameInformation)
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
		if(type==MNetRequestFile)
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
		if(type==MNetNotReadyToLaunch)
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
		if(type == MNetSendReteamingInformation)
		{
			if(joinedGame)
				joinedGame->recieveMessage(message);
		}
		if(type==MNetSendFileInformation)
		{
			shared_ptr<NetSendFileInformation> info = static_pointer_cast<NetSendFileInformation>(message);
			if(assembler[info->getFileID()])
				assembler[info->getFileID()]->handleMessage(message);
		}
		if(type==MNetSendFileChunk)
		{
			shared_ptr<NetSendFileChunk> info = static_pointer_cast<NetSendFileChunk>(message);
			if(assembler[info->getFileID()])
				assembler[info->getFileID()]->handleMessage(message);
		}
		if(type == MNetPing)
		{
			shared_ptr<NetPingReply> event(new NetPingReply);
			nc.sendMessage(event);
		}
		if(type == MNetPlayerIsBanned)
		{
			shared_ptr<YOGPlayerBannedEvent> event(new YOGPlayerBannedEvent);
			sendToListeners(event);
		}
		if(type == MNetIPIsBanned)
		{
			shared_ptr<YOGIPBannedEvent> event(new YOGIPBannedEvent);
			sendToListeners(event);
		}
		if(type == MNetAcceptMapUpload)
		{
			if(uploader)
				uploader->recieveMessage(message);
		}
		if(type == MNetRefuseMapUpload)
		{
			if(uploader)
				uploader->recieveMessage(message);
		}
		if(type == MNetDownloadableMapInfos)
		{
			downloadableMapList->recieveMessage(message);
		}
		if(type == MNetSendMapThumbnail)
		{
			downloadableMapList->recieveMessage(message);
		}
		message = nc.getMessage();
	}

	if(gameConnection)
	{		
		shared_ptr<NetMessage> message = gameConnection->getMessage();
		while(message)
		{
			Uint8 type = message->getMessageType();
			if(type==MNetSendOrder)
			{
				//ignore orders for when there is no joined game,
				//say, the leftover orders in transit after a player
				//quits a game
				if(joinedGame)
					joinedGame->recieveMessage(message);
			}
			message = gameConnection->getMessage();
		}
	}
	for(std::map<Uint16, boost::shared_ptr<YOGClientFileAssembler> >::iterator i = assembler.begin(); i!=assembler.end();)
	{
		if(i->second)
		{
			i->second->update();
			++i;
		}
		else
		{
			std::map<Uint16, boost::shared_ptr<YOGClientFileAssembler> >::iterator to_erase = i;
			i++;
			assembler.erase(to_erase);
		}
	}
}



const std::string& YOGClient::getIPAddress() const
{
	return nc.getIPAddress();
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



void YOGClient::addYOGClientChatChannel(YOGClientChatChannel* channel)
{
	chatChannels[channel->getChannelID()] = channel;
}



void YOGClient::removeYOGClientChatChannel(YOGClientChatChannel* channel)
{
	chatChannels.erase(channel->getChannelID());
}



void YOGClient::sendToListeners(boost::shared_ptr<YOGClientEvent> event)
{
	for(std::list<YOGClientEventListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->handleYOGClientEvent(event);
	}
}



void YOGClient::setYOGClientFileAssembler(Uint16 fileID, boost::shared_ptr<YOGClientFileAssembler> nassembler)
{
	assembler[fileID]=nassembler;
}



boost::shared_ptr<YOGClientFileAssembler> YOGClient::getYOGClientFileAssembler(Uint16 fileID)
{
	return assembler[fileID];
}



void YOGClient::addEventListener(YOGClientEventListener* listener)
{
	listeners.push_back(listener);
}



void YOGClient::removeEventListener(YOGClientEventListener* listener)
{
	listeners.remove(listener);
}



void YOGClient::setGameConnection(boost::shared_ptr<NetConnection> ngameConnection)
{
	gameConnection = ngameConnection;
}



boost::shared_ptr<NetConnection> YOGClient::getGameConnection()
{
	return gameConnection;
}



boost::shared_ptr<YOGClientBlockedList> YOGClient::getBlockedList()
{
	return blocked;
}



boost::shared_ptr<YOGClientCommandManager> YOGClient::getCommandManager()
{
	return commands;
}



YOGClientMapUploader* YOGClient::getMapUploader()
{
	return uploader;
}



void YOGClient::setMapUploader(YOGClientMapUploader* nuploader)
{
	uploader = nuploader;
}



boost::shared_ptr<YOGClientDownloadableMapList> YOGClient::getDownloadableMapList()
{
	return downloadableMapList;
}



boost::shared_ptr<YOGClientRatedMapList> YOGClient::getRatedMapList()
{
	return ratedMapList;
}



void YOGClient::setMapDownloader(YOGClientMapDownloader* ndownloader)
{
	downloader = ndownloader;
}


	
YOGClientMapDownloader* YOGClient::getMapDownloader()
{
	return downloader;
}



void YOGClient::attachGameServer(boost::shared_ptr<YOGServer> nserver)
{
	server = nserver;
}

	

boost::shared_ptr<YOGServer> YOGClient::getGameServer()
{
	return server;
}



void  YOGClient::setP2PConnection(boost::shared_ptr<P2PConnection> connection)
{
	p2pconnection = connection;
}



boost::shared_ptr<P2PConnection> YOGClient::getP2PConnection()
{
	return p2pconnection;
}



void YOGClient::setGameListManager(boost::shared_ptr<YOGClientGameListManager> ngameListManager)
{
	gameListManager = ngameListManager;
}



boost::shared_ptr<YOGClientGameListManager> YOGClient::getGameListManager()
{
	return gameListManager;
}



void YOGClient::setPlayerListManager(boost::shared_ptr<YOGClientPlayerListManager> nplayerListManager)
{
	playerListManager = nplayerListManager;
}



boost::shared_ptr<YOGClientPlayerListManager> YOGClient::getPlayerListManager()
{
	return playerListManager;
}



