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

#include "YOGPlayer.h"
#include "YOGGameServer.h"
#include "YOGMapDistributor.h"

YOGPlayer::YOGPlayer(shared_ptr<NetConnection> connection, Uint16 id, YOGGameServer& server)
 : connection(connection), server(server), playerID(id)
{
	connectionState = WaitingForClientInformation;
	gameListState=GameListWaiting;
	playerListState=PlayerListWaiting;
	loginState = YOGLoginUnknown;
	gameID=0;
	netVersion=0;
}



void YOGPlayer::update()
{
	//Send outgoing messages
	updateConnectionSates();
	updateGamePlayerLists();

	//Parse incoming messages.
	shared_ptr<NetMessage> message = connection->getMessage();
	if(!message)
		return;
	Uint8 type = message->getMessageType();
	//This recieves the client information
	if(type==MNetSendClientInformation)
	{
		shared_ptr<NetSendClientInformation> info = static_pointer_cast<NetSendClientInformation>(message);
		netVersion = info->getNetVersion();
		connectionState = NeedToSendServerInformation;
	}
	//This recieves a login attempt
	else if(type==MNetAttemptLogin)
	{
		shared_ptr<NetAttemptLogin> info = static_pointer_cast<NetAttemptLogin>(message);
		std::string username = info->getUsername();
		std::string password = info->getPassword();
		loginState = server.verifyLoginInformation(username, password, netVersion);
		if(loginState == YOGLoginSuccessful)
		{
			server.playerHasLoggedIn(username, playerID);
			playerName=username;
			connectionState = NeedToSendLoginAccepted;
			gameListState=UpdatingGameList;
			playerListState=UpdatingPlayerList;
		}
		else
		{
			connectionState = NeedToSendLoginRefusal;
		}	
	}	//This recieves a login attempt
	else if(type==MNetAttemptRegistration)
	{
		shared_ptr<NetAttemptRegistration> info = static_pointer_cast<NetAttemptRegistration>(message);
		std::string username = info->getUsername();
		std::string password = info->getPassword();
		loginState = server.registerInformation(username, password, netVersion);
		if(loginState == YOGLoginSuccessful)
		{
			server.playerHasLoggedIn(username, playerID);
			playerName=username;
			connectionState = NeedToSendRegistrationAccepted;
			gameListState=UpdatingGameList;
			playerListState=UpdatingPlayerList;
		}
		else
		{
			connectionState = NeedToSendRegistrationRefused;
		}	
	}
	//This recieves a YOGMessage and sends it to the game server to be proccessed
	else if(type==MNetSendYOGMessage)
	{
		shared_ptr<NetSendYOGMessage> info = static_pointer_cast<NetSendYOGMessage>(message);
		///This is a special override used to restart development server
		if(info->getMessage()->getSender() == "genixpro" && info->getMessage()->getMessage()=="server_restart")
			exit(0);
		server.getChatChannelManager().getChannel(info->getChannel())->routeMessage(info->getMessage(), server.getPlayer(playerID));
	}
	//This recieves an attempt to create a new game
	else if(type==MNetCreateGame)
	{
		shared_ptr<NetCreateGame> info = static_pointer_cast<NetCreateGame>(message);
		handleCreateGame(info->getGameName());
	}
	//This recieves an attempt to join a game
	else if(type==MNetAttemptJoinGame)
	{
		shared_ptr<NetAttemptJoinGame> info = static_pointer_cast<NetAttemptJoinGame>(message);
		handleJoinGame(info->getGameID());
	}
	//This recieves a message to set the map header
	else if(type==MNetSendMapHeader)
	{
		shared_ptr<NetSendMapHeader> info = static_pointer_cast<NetSendMapHeader>(message);
		game->setMapHeader(info->getMapHeader());
	}
	//This recieves a message to set the game header
	else if(type==MNetSendGameHeader)
	{
		shared_ptr<NetSendGameHeader> info = static_pointer_cast<NetSendGameHeader>(message);
		game->setGameHeader(info->getGameHeader());
	}
	//This recieves a message to set the game header
	else if(type==MNetSendGamePlayerInfo)
	{
		shared_ptr<NetSendGamePlayerInfo> info = static_pointer_cast<NetSendGamePlayerInfo>(message);
		info->downloadToGameHeader(game->getGameHeader());
		game->routeMessage(info, server.getPlayer(playerID));
	}
	//This recieves a message to set the game header
	else if(type==MNetStartGame)
	{
		game->startGame();
	}
	//This recieves routes an order
	else if(type==MNetSendOrder)
	{
		shared_ptr<NetSendOrder> info = static_pointer_cast<NetSendOrder>(message);
		game->routeOrder(info, server.getPlayer(playerID));
	}
	//This recieves requests a map file
	else if(type==MNetRequestMap)
	{
		game->getMapDistributor()->addMapRequestee(server.getPlayer(playerID));
	}
	//This recieves requests a map file
	else if(type==MNetRequestNextChunk)
	{
		game->getMapDistributor()->handleMessage(message, server.getPlayer(playerID));
	}
	//This recieves a file chunk
	else if(type==MNetSendFileChunk)
	{
		game->getMapDistributor()->handleMessage(message, server.getPlayer(playerID));
	}
	//This recieves a file information message
	else if(type==MNetSendFileInformation)
	{
		game->getMapDistributor()->handleMessage(message, server.getPlayer(playerID));
	}
	//This recieves a leave game message
	else if(type==MNetLeaveGame)
	{
		game->removePlayer(server.getPlayer(playerID));
	}
	//This recieves a ready to launch message
	else if(type==MNetReadyToLaunch)
	{
		shared_ptr<NetReadyToLaunch> info = static_pointer_cast<NetReadyToLaunch>(message);
		game->sendReadyToStart(info);
	}
	//This recieves a not ready to launch message
	else if(type==MNetNotReadyToLaunch)
	{
		shared_ptr<NetNotReadyToLaunch> info = static_pointer_cast<NetNotReadyToLaunch>(message);
		game->sendNotReadyToStart(info);
	}
	//This recieves a kick message
	else if(type==MNetKickPlayer)
	{
		shared_ptr<NetKickPlayer> info = static_pointer_cast<NetKickPlayer>(message);
		game->sendKickMessage(info);
	}
}



bool YOGPlayer::isConnected()
{
	return connection->isConnected();
}



void YOGPlayer::sendMessage(shared_ptr<NetMessage> message)
{
	connection->sendMessage(message);
}



void YOGPlayer::setPlayerID(Uint16 id)
{
	playerID=id;
}



Uint16 YOGPlayer::getPlayerID()
{
	return playerID;
}



Uint16 YOGPlayer::getGameID()
{
	return gameID;
}



std::string YOGPlayer::getPlayerName()
{
	return playerName;
}



boost::shared_ptr<YOGGame> YOGPlayer::getGame()
{
	return game;
}



void YOGPlayer::updateConnectionSates()
{
	//Send the server information
	if(connectionState==NeedToSendServerInformation)
	{
		shared_ptr<NetSendServerInformation> info(new NetSendServerInformation(server.getLoginPolicy(), server.getGamePolicy(), playerID));
		connection->sendMessage(info);
		connectionState = WaitingForLoginAttempt;
	}
	//Send the login accepted message
	if(connectionState==NeedToSendLoginAccepted)
	{
		shared_ptr<NetLoginSuccessful> accepted(new NetLoginSuccessful);
		connection->sendMessage(accepted);
		connectionState = ClientOnStandby;
	}
	//Send the login refused message
	if(connectionState==NeedToSendLoginRefusal)
	{
		shared_ptr<NetRefuseLogin> refused(new NetRefuseLogin(loginState));
		connection->sendMessage(refused);
		connectionState = WaitingForLoginAttempt;
	}
	//Send the login accepted message
	if(connectionState==NeedToSendRegistrationAccepted)
	{
		shared_ptr<NetAcceptRegistration> accepted(new NetAcceptRegistration);
		connection->sendMessage(accepted);
		connectionState = ClientOnStandby;
	}
	//Send the login refused message
	if(connectionState==NeedToSendRegistrationRefused)
	{
		shared_ptr<NetRefuseRegistration> refused(new NetRefuseRegistration(loginState));
		connection->sendMessage(refused);
		connectionState = WaitingForLoginAttempt;
	}
}

void YOGPlayer::updateGamePlayerLists()
{
	//Send an updated game list to the user
	if(gameListState==UpdatingGameList)
	{
		if(playersGames != server.getGameList())
		{
			shared_ptr<NetUpdateGameList> gamelist(new NetUpdateGameList);
			gamelist->updateDifferences(playersGames, server.getGameList());
			playersGames = server.getGameList();
			connection->sendMessage(gamelist);
		}
	}
	//Send an updated player list to the user
	if(playerListState==UpdatingPlayerList)
	{
		if(playersPlayerList != server.getPlayerList())
		{
			shared_ptr<NetUpdatePlayerList> playerlist(new NetUpdatePlayerList);
			playerlist->updateDifferences(playersPlayerList, server.getPlayerList());
			playersPlayerList = server.getPlayerList();
			connection->sendMessage(playerlist);
		}
	}
}


void YOGPlayer::handleCreateGame(const std::string& gameName)
{
	YOGGameCreateRefusalReason reason = server.canCreateNewGame(gameName);
	if(reason == YOGCreateRefusalUnknown)
	{
		gameID = server.createNewGame(gameName);
		game = server.getGame(gameID);
		shared_ptr<NetCreateGameAccepted> message(new NetCreateGameAccepted(game->getChatChannel()));
		connection->sendMessage(message);
		game->addPlayer(server.getPlayer(playerID));
	}
	else
	{
		shared_ptr<NetCreateGameRefused> message(new NetCreateGameRefused(reason));
		connection->sendMessage(message);
	}
}



void YOGPlayer::handleJoinGame(Uint16 ngameID)
{
	YOGGameJoinRefusalReason reason = server.canJoinGame(ngameID);
	if(reason == YOGJoinRefusalUnknown)
	{	
		gameID = ngameID;
		game = server.getGame(gameID);
		shared_ptr<NetGameJoinAccepted> message(new NetGameJoinAccepted(game->getChatChannel()));
		connection->sendMessage(message);
		game->addPlayer(server.getPlayer(playerID));
		//gameListState = NeedToSendGameList;
	}
	else
	{
		shared_ptr<NetGameJoinRefused> message(new NetGameJoinRefused(reason));
		connection->sendMessage(message);
	}
}


