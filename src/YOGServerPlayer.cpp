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
#include "YOGServerFileDistributor.h"
#include "YOGServerPlayer.h"
#include "SDLCompat.h"

using boost::static_pointer_cast;

YOGServerPlayer::YOGServerPlayer(shared_ptr<NetConnection> connection, Uint16 id, YOGServer& server)
 : connection(connection), server(server), playerID(id)
{
	connectionState = WaitingForClientInformation;
	gameListState=GameListWaiting;
	playerListState=PlayerListWaiting;
	loginState = YOGLoginUnknown;
	gameID=0;
	netVersion=0;
	pingCountdown=SDL_GetTicks64();
	pingSendTime=0;
	port = 0;
}



void YOGServerPlayer::update()
{
	//Send outgoing messages
	updateConnectionSates();
	updateGamePlayerLists();

	if((static_cast<Sint64>(SDL_GetTicks64()) - static_cast<Sint64>(pingCountdown)) > 5000 && pingCountdown != 0)
	{
		shared_ptr<NetPing> message(new NetPing);
		connection->sendMessage(message);
		pingSendTime = SDL_GetTicks64();
		pingCountdown = 0;
	}

	boost::shared_ptr<YOGServerGame> nGame;
	if(!game.expired())
	{
		nGame = boost::shared_ptr<YOGServerGame>(game);
	}

	//Parse incoming messages.
	shared_ptr<NetMessage> message = connection->getMessage();
	if(!message)
		return;
	Uint8 type = message->getMessageType();
	//This receives the client information
	if(type==MNetSendClientInformation)
	{
		shared_ptr<NetSendClientInformation> info = static_pointer_cast<NetSendClientInformation>(message);
		netVersion = info->getNetVersion();
		connectionState = NeedToSendServerInformation;
	}
	//This receives a login attempt
	else if(type==MNetAttemptLogin)
	{
		shared_ptr<NetAttemptLogin> info = static_pointer_cast<NetAttemptLogin>(message);
		std::string username = info->getUsername();
		std::string password = info->getPassword();
		loginState = server.verifyLoginInformation(username, password, getPlayerIP(), netVersion);
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
	}
	//This receives a login attempt
	else if(type==MNetAttemptRegistration)
	{
		shared_ptr<NetAttemptRegistration> info = static_pointer_cast<NetAttemptRegistration>(message);
		std::string username = info->getUsername();
		std::string password = info->getPassword();
		loginState = server.registerInformation(username, password, getPlayerIP(), netVersion);
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
	//This receives a YOGMessage and sends it to the game server to be processed
	else if(type==MNetSendYOGMessage)
	{
		shared_ptr<NetSendYOGMessage> info = static_pointer_cast<NetSendYOGMessage>(message);
		///This is sends a command to the administrator engine
		if(server.getAdministratorList().isAdministrator(info->getMessage()->getSender()))
			server.getAdministrator().executeAdministrativeCommand(info->getMessage()->getMessage(), server.getPlayer(playerID), false);
		///If this player is a moderator, also execute a command, however, moderators can only execute a limited number of commands
		else if(server.getPlayerStoredInfoManager().getPlayerStoredInfo(info->getMessage()->getSender()).isModerator())
			server.getAdministrator().executeAdministrativeCommand(info->getMessage()->getMessage(), server.getPlayer(playerID), true);
			
		
		///Check if this player is muted, ignore otherwise
		if(!server.getPlayerStoredInfoManager().getPlayerStoredInfo(info->getMessage()->getSender()).isMuted())
			server.getChatChannelManager().getChannel(info->getChannel())->routeMessage(info->getMessage(), server.getPlayer(playerID));
	}
	//This receives an attempt to create a new game
	else if(type==MNetCreateGame)
	{
		shared_ptr<NetCreateGame> info = static_pointer_cast<NetCreateGame>(message);
		handleCreateGame(info->getGameName());
	}
	//This receives a message to set the map header
	else if(type==MNetSendMapHeader)
	{
		shared_ptr<NetSendMapHeader> info = static_pointer_cast<NetSendMapHeader>(message);
		nGame->setMapHeader(info->getMapHeader());
	}
	//This receives an attempt to join a game
	else if(type==MNetAttemptJoinGame)
	{
		shared_ptr<NetAttemptJoinGame> info = static_pointer_cast<NetAttemptJoinGame>(message);
		handleJoinGame(info->getGameID());
	}
	//This receives a message to set the map header
	else if(type==MNetSendReTeamingInformation)
	{
		shared_ptr<NetSendReTeamingInformation> info = static_pointer_cast<NetSendReTeamingInformation>(message);
		nGame->setReTeamingInfo(info->getReTeamingInfo());
	}
	//This receives a request to change a players team in the game
	else if(type==MNetRequestGameStart)
	{
		shared_ptr<NetRequestGameStart> info = static_pointer_cast<NetRequestGameStart>(message);
		nGame->receiveGameStartRequest();
	}
	//This receives a message to set the game header
	else if(type==MNetSendGameHeader)
	{
		shared_ptr<NetSendGameHeader> info = static_pointer_cast<NetSendGameHeader>(message);
		info->downloadToGameHeader(nGame->getGameHeader());
		nGame->routeMessage(info, server.getPlayer(playerID));
	}
	//This receives a message to set the game header
	else if(type==MNetSendGamePlayerInfo)
	{
		shared_ptr<NetSendGamePlayerInfo> info = static_pointer_cast<NetSendGamePlayerInfo>(message);
		info->downloadToGameHeader(nGame->getGameHeader());
		nGame->routeMessage(info, server.getPlayer(playerID));
	}
	//This receives a message to set the game header
	else if(type==MNetStartGame)
	{
		nGame->startGame();
	}
	//This receives requests a map file
	else if(type==MNetRequestFile)
	{
		shared_ptr<NetRequestFile> info = static_pointer_cast<NetRequestFile>(message);
		if(server.getFileDistributionManager().getDistributor(info->getFileID()))
		{
			server.getFileDistributionManager().getDistributor(info->getFileID())->addMapRequestee(server.getPlayer(playerID));
		}
	}
	//This receives a file chunk
	else if(type==MNetSendFileChunk)
	{
		shared_ptr<NetSendFileChunk> info = static_pointer_cast<NetSendFileChunk>(message);
		if(server.getFileDistributionManager().getDistributor(info->getFileID()))
		{
			server.getFileDistributionManager().getDistributor(info->getFileID())->handleMessage(message, server.getPlayer(playerID));
		}
	}
	//This receives a file information message
	else if(type==MNetSendFileInformation)
	{
		shared_ptr<NetSendFileInformation> info = static_pointer_cast<NetSendFileInformation>(message);
		if(server.getFileDistributionManager().getDistributor(info->getFileID()))
		{
			server.getFileDistributionManager().getDistributor(info->getFileID())->handleMessage(message, server.getPlayer(playerID));
		}
	}
	//This receives a leave game message
	else if(type==MNetLeaveGame)
	{
		nGame->removePlayer(server.getPlayer(playerID));
	}
	//This receives a ready to launch message
	else if(type==MNetReadyToLaunch)
	{
		shared_ptr<NetReadyToLaunch> info = static_pointer_cast<NetReadyToLaunch>(message);
		nGame->setReadyToStart(playerID);
	}
	//This receives a not ready to launch message
	else if(type==MNetNotReadyToLaunch)
	{
		shared_ptr<NetNotReadyToLaunch> info = static_pointer_cast<NetNotReadyToLaunch>(message);
		nGame->setNotReadyToStart(playerID);
	}
	//This receives a kick message
	else if(type==MNetKickPlayer)
	{
		shared_ptr<NetKickPlayer> info = static_pointer_cast<NetKickPlayer>(message);
		nGame->kickPlayer(info);
	}
	//This receives a request to add an AI player to the game
	else if(type==MNetAddAI)
	{
		shared_ptr<NetAddAI> info = static_pointer_cast<NetAddAI>(message);
		nGame->addAIPlayer(static_cast<AI::ImplementationID>(info->getType()));
	}
	//This receives a request to add an AI player to the game
	else if(type==MNetRemoveAI)
	{
		shared_ptr<NetRemoveAI> info = static_pointer_cast<NetRemoveAI>(message);
		nGame->removeAIPlayer(info->getPlayerNumber());
	}
	//This receives a request to change a players team in the game
	else if(type==MNetChangePlayersTeam)
	{
		shared_ptr<NetChangePlayersTeam> info = static_pointer_cast<NetChangePlayersTeam>(message);
		nGame->setTeam(info->getPlayer(), info->getTeam());
	}
	//This receives a request to change a players team in the game
	else if(type==MNetRequestGameStart)
	{
		shared_ptr<NetRequestGameStart> info = static_pointer_cast<NetRequestGameStart>(message);
		nGame->receiveGameStartRequest();
	}
	//This receives a ping reply
	else if(type==MNetPingReply)
	{
		shared_ptr<NetPingReply> info = static_pointer_cast<NetPingReply>(message);
		pings.push_back(std::max<Sint64>(0, static_cast<Sint64>(SDL_GetTicks64()) - static_cast<Sint64>(pingSendTime)));
		if(pings.size() > 16)
			pings.erase(pings.begin());

		pingCountdown = SDL_GetTicks64();
	}
	//This receives a ping reply
	else if(type==MNetSendGameResult)
	{
		shared_ptr<NetSendGameResult> info = static_pointer_cast<NetSendGameResult>(message);
		nGame->setPlayerGameResult(server.getPlayer(playerID), info->getGameResult()); 
	}
	//This receives a ping reply
	else if(type==MNetRequestDownloadableMapList)
	{
		shared_ptr<NetRequestDownloadableMapList> info = static_pointer_cast<NetRequestDownloadableMapList>(message);
		server.getMapDatabank().sendMapListToPlayer(server.getPlayer(playerID));
	}
	//This receives
	else if(type==MNetRequestMapUpload)
	{
		shared_ptr<NetRequestMapUpload> info = static_pointer_cast<NetRequestMapUpload>(message);
		YOGMapUploadRefusalReason reason = server.getMapDatabank().canReceiveFromPlayer(info->getMapInfo());
		if(reason == YOGMapUploadReasonUnknown)
		{
			Uint16 fileID =  server.getMapDatabank().receiveMapFromPlayer(info->getMapInfo(), server.getPlayer(playerID));
			boost::shared_ptr<NetAcceptMapUpload> info = boost::shared_ptr<NetAcceptMapUpload>(new NetAcceptMapUpload(fileID));
			sendMessage(info);
		}
		else
		{
			boost::shared_ptr<NetRefuseMapUpload> info = boost::shared_ptr<NetRefuseMapUpload>(new NetRefuseMapUpload(reason));
			sendMessage(info);
		}
	}
	//This receives a cancel to a file upload
	else if(type==MNetCancelSendingFile)
	{
		shared_ptr<NetCancelSendingFile> info = static_pointer_cast<NetCancelSendingFile>(message);
		if(server.getFileDistributionManager().getDistributor(info->getFileID()))
		{
			server.getFileDistributionManager().getDistributor(info->getFileID())->handleMessage(info, server.getPlayer(playerID));
		}
	}
	//This receives a cancel to a file download
	else if(type==MNetCancelReceivingFile)
	{
		shared_ptr<NetCancelReceivingFile> info = static_pointer_cast<NetCancelReceivingFile>(message);
		if(server.getFileDistributionManager().getDistributor(info->getFileID()))
		{
			server.getFileDistributionManager().getDistributor(info->getFileID())->removeMapRequestee(server.getPlayer(playerID));
		}
	}
	//This receives a cancel to a file download
	else if(type==MNetRequestMapThumbnail)
	{
		shared_ptr<NetRequestMapThumbnail> info = static_pointer_cast<NetRequestMapThumbnail>(message);
		server.getMapDatabank().sendMapThumbnailToPlayer(info->getMapID(), server.getPlayer(playerID));
	}
	//This receives a map rating
	else if(type==MNetSubmitRatingOnMap)
	{
		shared_ptr<NetSubmitRatingOnMap> info = static_pointer_cast<NetSubmitRatingOnMap>(message);
		server.getMapDatabank().submitRating(info->getMapID(), info->getRating());
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
	std::vector<unsigned> sPings(pings.begin(), pings.end());
	std::sort(sPings.begin(), sPings.end(), std::greater<unsigned>());
	if(sPings.size() > 2)
		sPings.erase(sPings.begin());
	if(sPings.size() > 2)
		sPings.erase(sPings.end()-1);
	if(sPings.size() > 2)
		sPings.erase(sPings.begin());
	if(sPings.size() > 2)
		sPings.erase(sPings.end()-1);
	
	//Compute mean
	unsigned mean = 0;
	for(std::vector<unsigned>::iterator i=sPings.begin(); i!=sPings.end(); ++i)
	{
		mean += *i;
	}
	mean /= sPings.size();
	
	//Compute standard deviations
	float deviation = 0;
	for(std::vector<unsigned>::iterator i=sPings.begin(); i!=sPings.end(); ++i)
	{
		deviation += float((*i - mean) * (*i - mean));
	}
	deviation = std::sqrt(deviation / sPings.size());

	//At two standard deviations, 99.7% of all data will be less
	return mean + int(deviation*2);
}



int YOGServerPlayer::getP2PPort()
{
	return port;
}



void YOGServerPlayer::closeConnection()
{
	connection->closeConnection();
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
			shared_ptr<NetUpdateGameList> gameList(new NetUpdateGameList);
			gameList->updateDifferences(playersGames, server.getGameList());
			playersGames = server.getGameList();
			connection->sendMessage(gameList);
		}
	}
	//Send an updated player list to the user
	if(playerListState==UpdatingPlayerList)
	{
		if(playersPlayerList != server.getPlayerList())
		{
			shared_ptr<NetUpdatePlayerList> playerList(new NetUpdatePlayerList);
			playerList->updateDifferences(playersPlayerList, server.getPlayerList());
			playersPlayerList = server.getPlayerList();
			connection->sendMessage(playerList);
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
		boost::shared_ptr<YOGServerGame> nGame(game);
		updateGamePlayerLists();
		std::string ip = boost::shared_ptr<YOGServerGame>(game)->getRouterIP();
		shared_ptr<NetCreateGameAccepted> message(new NetCreateGameAccepted(nGame->getChatChannel(), gameID, ip, nGame->getFileID()));
		connection->sendMessage(message);
		nGame->addPlayer(server.getPlayer(playerID));
	}
	else
	{
		shared_ptr<NetCreateGameRefused> message(new NetCreateGameRefused(reason));
		connection->sendMessage(message);
	}
}



void YOGServerPlayer::handleJoinGame(Uint16 nGameID)
{
	YOGServerGameJoinRefusalReason reason = server.canJoinGame(nGameID);
	if(reason == YOGJoinRefusalUnknown)
	{	
		gameID = nGameID;
		game = server.getGame(gameID);
		boost::shared_ptr<YOGServerGame> nGame(game);
		shared_ptr<NetGameJoinAccepted> message(new NetGameJoinAccepted(nGame->getChatChannel()));
		connection->sendMessage(message);
		nGame->addPlayer(server.getPlayer(playerID));
		//gameListState = NeedToSendGameList;
	}
	else
	{
		shared_ptr<NetGameJoinRefused> message(new NetGameJoinRefused(reason));
		connection->sendMessage(message);
	}
}


