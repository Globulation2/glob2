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

#include "NetMessage.h"
#include "YOGServerChatChannel.h"
#include "YOGServerGame.h"
#include "YOGServer.h"
#include "YOGServerMapDistributor.h"
#include "YOGServerPlayer.h"
#include "P2PManager.h"

YOGServerPlayer::YOGServerPlayer(shared_ptr<NetConnection> connection, Uint16 id, YOGServer& server)
 : connection(connection), server(server), playerID(id)
{
	connectionState = WaitingForClientInformation;
	gameListState=GameListWaiting;
	playerListState=PlayerListWaiting;
	loginState = YOGLoginUnknown;
	gameID=0;
	netVersion=0;
	pingCountdown=SDL_GetTicks();
	pingSendTime=0;
	port = 0;
	p2p=NULL;
}



void YOGServerPlayer::update()
{
	//Send outgoing messages
	updateConnectionSates();
	updateGamePlayerLists();

	if(SDL_GetTicks() - pingCountdown > 5000 && pingCountdown != 0)
	{
		shared_ptr<NetPing> message(new NetPing);
		connection->sendMessage(message);
		pingSendTime = SDL_GetTicks();
		pingCountdown = 0;
	}

	boost::shared_ptr<YOGServerGame> ngame;
	if(!game.expired())
	{
		ngame = boost::shared_ptr<YOGServerGame>(game);
	}

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
		if(server.getAdministratorList().isAdministrator(info->getMessage()->getSender()))
			server.getAdministrator().executeAdministrativeCommand(info->getMessage()->getMessage(), server.getPlayer(playerID));
		///Check if this player is muted, ignore otherwise
		if(!server.getPlayerStoredInfoManager().getPlayerStoredInfo(info->getMessage()->getSender()).isMuted())
			server.getChatChannelManager().getChannel(info->getChannel())->routeMessage(info->getMessage(), server.getPlayer(playerID));
	}
	//This recieves an attempt to create a new game
	else if(type==MNetCreateGame)
	{
		shared_ptr<NetCreateGame> info = static_pointer_cast<NetCreateGame>(message);
		handleCreateGame(info->getGameName());
	}
	//This recieves a message to set the map header
	else if(type==MNetSendMapHeader)
	{
		shared_ptr<NetSendMapHeader> info = static_pointer_cast<NetSendMapHeader>(message);
		ngame->setMapHeader(info->getMapHeader());
	}
	//This recieves an attempt to join a game
	else if(type==MNetAttemptJoinGame)
	{
		shared_ptr<NetAttemptJoinGame> info = static_pointer_cast<NetAttemptJoinGame>(message);
		handleJoinGame(info->getGameID());
	}
	//This recieves a message to set the map header
	else if(type==MNetSendReteamingInformation)
	{
		shared_ptr<NetSendReteamingInformation> info = static_pointer_cast<NetSendReteamingInformation>(message);
		ngame->setReteamingInfo(info->getReteamingInfo());
	}
	//This recieves a request to change a players team in the game
	else if(type==MNetRequestGameStart)
	{
		shared_ptr<NetRequestGameStart> info = static_pointer_cast<NetRequestGameStart>(message);
		ngame->recieveGameStartRequest();
	}
	//This recieves a message to set the game header
	else if(type==MNetSendGameHeader)
	{
		shared_ptr<NetSendGameHeader> info = static_pointer_cast<NetSendGameHeader>(message);
		info->downloadToGameHeader(ngame->getGameHeader());
		ngame->routeMessage(info, server.getPlayer(playerID));
	}
	//This recieves a message to set the game header
	else if(type==MNetSendGamePlayerInfo)
	{
		shared_ptr<NetSendGamePlayerInfo> info = static_pointer_cast<NetSendGamePlayerInfo>(message);
		info->downloadToGameHeader(ngame->getGameHeader());
		ngame->routeMessage(info, server.getPlayer(playerID));
	}
	//This recieves a message to set the game header
	else if(type==MNetStartGame)
	{
		ngame->startGame();
	}
	//This recieves routes an order
	else if(type==MNetSendOrder)
	{
		if(p2p)
		{
			shared_ptr<NetSendOrder> info = static_pointer_cast<NetSendOrder>(message);
			p2p->recieveMessage(info, server.getPlayer(playerID));
		}
	}
	//This recieves requests a map file
	else if(type==MNetRequestMap)
	{
		ngame->getMapDistributor()->addMapRequestee(server.getPlayer(playerID));
	}
	//This recieves requests a map file
	else if(type==MNetRequestNextChunk)
	{
		ngame->getMapDistributor()->handleMessage(message, server.getPlayer(playerID));
	}
	//This recieves a file chunk
	else if(type==MNetSendFileChunk)
	{
		ngame->getMapDistributor()->handleMessage(message, server.getPlayer(playerID));
	}
	//This recieves a file information message
	else if(type==MNetSendFileInformation)
	{
		ngame->getMapDistributor()->handleMessage(message, server.getPlayer(playerID));
	}
	//This recieves a leave game message
	else if(type==MNetLeaveGame)
	{
		ngame->removePlayer(server.getPlayer(playerID));
	}
	//This recieves a ready to launch message
	else if(type==MNetReadyToLaunch)
	{
		shared_ptr<NetReadyToLaunch> info = static_pointer_cast<NetReadyToLaunch>(message);
		ngame->setReadyToStart(playerID);
	}
	//This recieves a not ready to launch message
	else if(type==MNetNotReadyToLaunch)
	{
		shared_ptr<NetNotReadyToLaunch> info = static_pointer_cast<NetNotReadyToLaunch>(message);
		ngame->setNotReadyToStart(playerID);
	}
	//This recieves a kick message
	else if(type==MNetKickPlayer)
	{
		shared_ptr<NetKickPlayer> info = static_pointer_cast<NetKickPlayer>(message);
		ngame->kickPlayer(info);
	}
	//This recieves a request to add an AI player to the game
	else if(type==MNetAddAI)
	{
		shared_ptr<NetAddAI> info = static_pointer_cast<NetAddAI>(message);
		ngame->addAIPlayer(static_cast<AI::ImplementitionID>(info->getType()));
	}
	//This recieves a request to add an AI player to the game
	else if(type==MNetRemoveAI)
	{
		shared_ptr<NetRemoveAI> info = static_pointer_cast<NetRemoveAI>(message);
		ngame->removeAIPlayer(info->getPlayerNumber());
	}
	//This recieves a request to change a players team in the game
	else if(type==MNetChangePlayersTeam)
	{
		shared_ptr<NetChangePlayersTeam> info = static_pointer_cast<NetChangePlayersTeam>(message);
		ngame->setTeam(info->getPlayer(), info->getTeam());
	}
	//This recieves a request to change a players team in the game
	else if(type==MNetRequestGameStart)
	{
		shared_ptr<NetRequestGameStart> info = static_pointer_cast<NetRequestGameStart>(message);
		ngame->recieveGameStartRequest();
	}
	//This recieves a ping reply
	else if(type==MNetPingReply)
	{
		shared_ptr<NetPingReply> info = static_pointer_cast<NetPingReply>(message);
		pings.push_back(SDL_GetTicks() - pingSendTime);
		if(pings.size() > 16)
			pings.erase(pings.begin());

		pingCountdown = SDL_GetTicks();
	}
	//This recieves a ping reply
	else if(type==MNetSetPlayerLocalPort)
	{
		shared_ptr<NetSetPlayerLocalPort> info = static_pointer_cast<NetSetPlayerLocalPort>(message);
		port = info->getPort();
	}
}



bool YOGServerPlayer::isConnected()
{
	return connection->isConnected();
}



void YOGServerPlayer::sendMessage(shared_ptr<NetMessage> message)
{
	connection->sendMessage(message);
}



void YOGServerPlayer::setPlayerID(Uint16 id)
{
	playerID=id;
}



Uint16 YOGServerPlayer::getPlayerID()
{
	return playerID;
}



Uint16 YOGServerPlayer::getGameID()
{
	return gameID;
}



std::string YOGServerPlayer::getPlayerName()
{
	return playerName;
}



std::string YOGServerPlayer::getPlayerIP()
{
	return connection->getIPAddress();
}



boost::shared_ptr<YOGServerGame> YOGServerPlayer::getGame()
{
	return boost::shared_ptr<YOGServerGame>(game);
}



unsigned YOGServerPlayer::getAveragePing() const
{
	if(pings.size() == 1)
	{
		return *pings.begin();
	}
	else if(pings.size() == 0)
	{
		return 0;
	}


	//Copy the ping values, sort them, remove two highest and two lowest
	std::vector<unsigned> spings(pings.begin(), pings.end());
	std::sort(spings.begin(), spings.end(), std::greater<unsigned>());
	if(spings.size() > 2)
		spings.erase(spings.begin());
	if(spings.size() > 2)
		spings.erase(spings.end()-1);
	if(spings.size() > 2)
		spings.erase(spings.begin());
	if(spings.size() > 2)
		spings.erase(spings.end()-1);
	
	//Compute mean
	unsigned mean = 0;
	for(std::vector<unsigned>::iterator i=spings.begin(); i!=spings.end(); ++i)
	{
		mean += *i;
	}
	mean /= spings.size();
	
	//Compute standard deviations
	float deviation = 0;
	for(std::vector<unsigned>::iterator i=spings.begin(); i!=spings.end(); ++i)
	{
		deviation += float((*i - mean) * (*i - mean));
	}
	deviation = std::sqrt(deviation / spings.size());

	//At two standard deviations, 99.7% of all data will be less
	return mean + int(deviation*2);
}



void YOGServerPlayer::setP2PManager(P2PManager* manager)
{
	p2p = manager;
}



P2PManager* YOGServerPlayer::getP2PManager()
{
	return p2p;
}



int YOGServerPlayer::getP2PPort()
{
	return port;
}




void YOGServerPlayer::updateConnectionSates()
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

void YOGServerPlayer::updateGamePlayerLists()
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


void YOGServerPlayer::handleCreateGame(const std::string& gameName)
{
	YOGServerGameCreateRefusalReason reason = server.canCreateNewGame(gameName);
	if(reason == YOGCreateRefusalUnknown)
	{
		gameID = server.createNewGame(gameName);
		game = server.getGame(gameID);
		boost::shared_ptr<YOGServerGame> ngame(game);
		shared_ptr<NetCreateGameAccepted> message(new NetCreateGameAccepted(ngame->getChatChannel()));
		connection->sendMessage(message);
		ngame->addPlayer(server.getPlayer(playerID));
	}
	else
	{
		shared_ptr<NetCreateGameRefused> message(new NetCreateGameRefused(reason));
		connection->sendMessage(message);
	}
}



void YOGServerPlayer::handleJoinGame(Uint16 ngameID)
{
	YOGServerGameJoinRefusalReason reason = server.canJoinGame(ngameID);
	if(reason == YOGJoinRefusalUnknown)
	{	
		gameID = ngameID;
		game = server.getGame(gameID);
		boost::shared_ptr<YOGServerGame> ngame(game);
		shared_ptr<NetGameJoinAccepted> message(new NetGameJoinAccepted(ngame->getChatChannel()));
		connection->sendMessage(message);
		ngame->addPlayer(server.getPlayer(playerID));
		//gameListState = NeedToSendGameList;
	}
	else
	{
		shared_ptr<NetGameJoinRefused> message(new NetGameJoinRefused(reason));
		connection->sendMessage(message);
	}
}


