// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include <iostream>
#include "MultiplayerGame.h"
#include "AuthMessages.h"
#include "FileTransferMessages.h"
#include "GameCreateMessages.h"
#include "LobbyMessages.h"
#include "OrderMessages.h"
#include "RegistrationMessages.h"
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
#include "YOGServer.h"

using std::static_pointer_cast;
using std::shared_ptr;

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
			ratedMapList = std::shared_ptr<YOGClientRatedMapList>(new YOGClientRatedMapList(username));
			blocked = std::shared_ptr<YOGClientBlockedList>(new YOGClientBlockedList(username));
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
			ratedMapList = std::shared_ptr<YOGClientRatedMapList>(new YOGClientRatedMapList(username));
			blocked = std::shared_ptr<YOGClientBlockedList>(new YOGClientBlockedList(username));
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
	for(std::map<Uint16, std::shared_ptr<YOGClientFileAssembler> >::iterator i = assembler.begin(); i!=assembler.end();)
	{
		if(i->second)
		{
			i->second->update();
			++i;
		}
		else
		{
			std::map<Uint16, std::shared_ptr<YOGClientFileAssembler> >::iterator to_erase = i;
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



void YOGClient::setMultiplayerGame(std::shared_ptr<MultiplayerGame> game)
{
	// The game-router connection belongs to the joined game. Whenever the
	// joined game is detached (cleared on exit, or replaced by a new one),
	// tear the router connection down so a stale/leaked socket from the
	// previous game cannot bleed into the next one. This is the single point
	// that guarantees the connection is closed on every exit path (leave,
	// kick, host-cancel, server-disconnect), all of which funnel through here.
	if(joinedGame != game)
		closeGameConnection();
	joinedGame=game;
}



std::shared_ptr<MultiplayerGame> YOGClient::getMultiplayerGame()
{
	return joinedGame;
}



void YOGClient::sendNetMessage(std::shared_ptr<NetMessage> message)
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



void YOGClient::sendToListeners(std::shared_ptr<YOGClientEvent> event)
{
	for(std::list<YOGClientEventListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->handleYOGClientEvent(event);
	}
}



void YOGClient::setYOGClientFileAssembler(Uint16 fileID, std::shared_ptr<YOGClientFileAssembler> nassembler)
{
	assembler[fileID]=nassembler;
}



std::shared_ptr<YOGClientFileAssembler> YOGClient::getYOGClientFileAssembler(Uint16 fileID)
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



void YOGClient::setGameConnection(std::shared_ptr<NetConnection> ngameConnection)
{
	gameConnection = ngameConnection;
}



std::shared_ptr<NetConnection> YOGClient::getGameConnection()
{
	return gameConnection;
}



void YOGClient::closeGameConnection()
{
	if(gameConnection)
		gameConnection->closeConnection();
}



std::shared_ptr<YOGClientBlockedList> YOGClient::getBlockedList()
{
	return blocked;
}



std::shared_ptr<YOGClientCommandManager> YOGClient::getCommandManager()
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



std::shared_ptr<YOGClientDownloadableMapList> YOGClient::getDownloadableMapList()
{
	return downloadableMapList;
}



std::shared_ptr<YOGClientRatedMapList> YOGClient::getRatedMapList()
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



void YOGClient::attachGameServer(std::shared_ptr<YOGServer> nserver)
{
	server = nserver;
}

	

std::shared_ptr<YOGServer> YOGClient::getGameServer()
{
	return server;
}



void  YOGClient::setP2PConnection(std::shared_ptr<P2PConnection> connection)
{
	p2pconnection = connection;
}



std::shared_ptr<P2PConnection> YOGClient::getP2PConnection()
{
	return p2pconnection;
}



void YOGClient::setGameListManager(std::shared_ptr<YOGClientGameListManager> ngameListManager)
{
	gameListManager = ngameListManager;
}



std::shared_ptr<YOGClientGameListManager> YOGClient::getGameListManager()
{
	return gameListManager;
}



void YOGClient::setPlayerListManager(std::shared_ptr<YOGClientPlayerListManager> nplayerListManager)
{
	playerListManager = nplayerListManager;
}



std::shared_ptr<YOGClientPlayerListManager> YOGClient::getPlayerListManager()
{
	return playerListManager;
}



