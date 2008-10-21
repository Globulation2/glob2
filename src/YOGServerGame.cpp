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

#include <algorithm>
#include "NetMessage.h"
#include "YOGServerChatChannel.h"
#include "YOGServerGame.h"
#include "YOGServer.h"
#include "YOGServerFileDistributor.h"
#include "YOGServerPlayer.h"
#include "YOGAfterJoinGameInformation.h"

YOGServerGame::YOGServerGame(Uint16 gameID, Uint32 chatChannel, const std::string& routerIP, YOGServer& server)
	: playerManager(gameHeader), gameID(gameID), chatChannel(chatChannel), routerIP(routerIP), server(server)
{
	requested=false;
	gameStarted=false;
	oldReadyToLaunch=false;
	recievedMapHeader=false;
	hasAddedHost=false;
	latencyMode = 0;
	latencyUpdateTimer = SDL_GetTicks();
	aiNum = 0;
	mapFile = server.getFileDistributionManager().allocateFileDistributor();
}


void YOGServerGame::update()
{
	if((SDL_GetTicks() - latencyUpdateTimer) > 4000)
	{
		chooseLatencyMode();
	}


	for(std::vector<shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end();)
	{
		if(!(*i)->isConnected())
		{
			//if the game has started, send a PlayerQuitsGameOrder on the
			//players behalf
			int p = 0;
			for(int j=0; j<gameHeader.getNumberOfPlayers(); ++j)
			{
				if(gameHeader.getBasePlayer(j).playerID == (*i)->getPlayerID())
				{
					p = j;
					break;
				}
			}
			boost::shared_ptr<Order> order(new PlayerQuitsGameOrder(p));
			order->sender = p;
			shared_ptr<NetSendOrder> message(new NetSendOrder(order));
			for(std::vector<shared_ptr<YOGServerPlayer> >::iterator j = players.begin(); j!=players.end(); ++j)
			{
				if ((*j) != (*i))
					(*j)->sendMessage(message);
			}

			size_t pos = i - players.begin();
			removePlayer(*i);
			i = players.begin() + pos;
		}
		else
		{
			i++;
		}
	}
}

void YOGServerGame::addPlayer(shared_ptr<YOGServerPlayer> player)
{
	if(players.size()==0)
	{
		setHost(player);
	}
	else
	{
		YOGAfterJoinGameInformation info;
		info.setMapHeader(mapHeader);
		info.setGameHeader(gameHeader);
		info.setLatencyAdjustment(latencyMode);
		info.setReteamingInformation(reteamingInfo);
		info.setGameRouterIP(routerIP);
		info.setMapFileID(mapFile);
		shared_ptr<NetSendAfterJoinGameInformation> afterjoin(new NetSendAfterJoinGameInformation(info));
		player->sendMessage(afterjoin);
		///If its the host, we don't add them until we've recieved the NetReteamingInformation
		playerManager.addPerson(player->getPlayerID(), player->getPlayerName());
	}
	players.push_back(player);
	//Add the player to the chat channel for communication
	server.getChatChannelManager().getChannel(chatChannel)->addPlayer(player);

	shared_ptr<NetPlayerJoinsGame> sendGamePlayerInfo(new NetPlayerJoinsGame(player->getPlayerID(), player->getPlayerName()));
	routeMessage(sendGamePlayerInfo);

	chooseLatencyMode();

	server.getGameInfo(gameID).setPlayersJoined(players.size());
}



void YOGServerGame::addAIPlayer(AI::ImplementitionID type)
{
	playerManager.addAIPlayer(type);

	shared_ptr<NetAddAI> addAI(new NetAddAI(static_cast<Uint8>(type)));
	routeMessage(addAI, host);

	aiNum+=1;
	server.getGameInfo(gameID).setAIJoined(aiNum);
}



void YOGServerGame::removePlayer(shared_ptr<YOGServerPlayer> player)
{
	std::vector<shared_ptr<YOGServerPlayer> >::iterator i = std::find(players.begin(), players.end(), player);
	if(i!=players.end())
		players.erase(i);

	if(!gameStarted)
	{
		if(player!=host)
		{
			playerManager.removePerson(player->getPlayerID());
		}
		else
		{
			//Host disconnected, remove all the other players
			for(std::vector<shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end();)
			{
				if((*i) != host)
				{
					shared_ptr<NetKickPlayer> message(new NetKickPlayer((*i)->getPlayerID(), YOGHostDisconnect));
					(*i)->sendMessage(message);
					i = players.erase(i);
				}
			}
		}
	}
	else
	{
		setPlayerGameResult(player, YOGGameResultConnectionLost);
	}

	//Remove the player from the chat channel
	server.getChatChannelManager().getChannel(chatChannel)->removePlayer(player);

	shared_ptr<NetSendGamePlayerInfo> sendGamePlayerInfo(new NetSendGamePlayerInfo(gameHeader));
	routeMessage(sendGamePlayerInfo);

	server.getFileDistributionManager().getDistributor(mapFile)->removeMapRequestee(player);

	chooseLatencyMode();

	server.getGameInfo(gameID).setPlayersJoined(players.size());
}



void YOGServerGame::removeAIPlayer(int playerNum)
{
	playerManager.removePlayer(playerNum);

	shared_ptr<NetRemoveAI> removeAI(new NetRemoveAI(playerNum));
	routeMessage(removeAI, host);

	aiNum-=1;
	server.getGameInfo(gameID).setAIJoined(aiNum);
}



void YOGServerGame::setTeam(int playerNum, int teamNum)
{
	playerManager.changeTeamNumber(playerNum, teamNum);

	shared_ptr<NetChangePlayersTeam> changeTeam(new NetChangePlayersTeam(playerNum, teamNum));
	routeMessage(changeTeam, host);
}



void YOGServerGame::setHost(shared_ptr<YOGServerPlayer> player)
{
	host = player;
}




void YOGServerGame::setMapHeader(const MapHeader& nmapHeader)
{
	mapHeader = nmapHeader;
	playerManager.setNumberOfTeams(mapHeader.getNumberOfTeams());
	server.getGameInfo(gameID).setMapName(mapHeader.getMapName());
	server.getGameInfo(gameID).setNumberOfTeams(mapHeader.getNumberOfTeams());
	recievedMapHeader=true;
	server.getFileDistributionManager().getDistributor(mapFile)->loadFromPlayer(host);
}



void YOGServerGame::setReteamingInfo(const NetReteamingInformation& nreteamingInfo)
{
	reteamingInfo=nreteamingInfo;
	playerManager.setReteamingInformation(reteamingInfo);
	
	if(!hasAddedHost)
	{
		hasAddedHost=true;
		playerManager.addPerson(host->getPlayerID(), host->getPlayerName());
	}
}



GameHeader& YOGServerGame::getGameHeader()
{
	return gameHeader;
}



void YOGServerGame::routeMessage(shared_ptr<NetMessage> message, shared_ptr<YOGServerPlayer> sender)
{
	for(std::vector<shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i) != sender)
			(*i)->sendMessage(message);
	}
}



void YOGServerGame::kickPlayer(shared_ptr<NetKickPlayer> message)
{
	routeMessage(message, host);	
	for(std::vector<shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i)->getPlayerID() == message->getPlayerID())
		{
			removePlayer(*i);
			break;
		}
	}
}



bool YOGServerGame::isEmpty() const
{
	return players.empty();
}



Uint16 YOGServerGame::getGameID() const
{
	return gameID;
}



void YOGServerGame::setReadyToStart(int playerID)
{
	playerManager.setReadyToGo(playerID, true);
	boost::shared_ptr<NetReadyToLaunch> message(new NetReadyToLaunch(playerID));
	for(std::vector<boost::shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i)->getPlayerID() != playerID)
			(*i)->sendMessage(message);
	}
}



void YOGServerGame::setNotReadyToStart(int playerID)
{
	playerManager.setReadyToGo(playerID, false);
	boost::shared_ptr<NetNotReadyToLaunch> message(new NetNotReadyToLaunch(playerID));
	for(std::vector<boost::shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i)->getPlayerID() != playerID)
			(*i)->sendMessage(message);
	}
}



void YOGServerGame::recieveGameStartRequest()
{
	if(playerManager.isEveryoneReadyToGo())
	{
		if(!gameStarted)
			startGame();
	}
	else
	{
		boost::shared_ptr<NetRefuseGameStart> message(new NetRefuseGameStart(YOGNotAllPlayersReady));
		host->sendMessage(message);
	}
}



void YOGServerGame::startGame()
{
	chooseLatencyMode();
	gameStarted=true;
	boost::shared_ptr<NetStartGame> message(new NetStartGame);
	routeMessage(message);
	server.getGameInfo(gameID).setGameState(YOGGameInfo::GameRunning);
}



Uint32 YOGServerGame::getChatChannel() const
{
	return chatChannel;
}



bool YOGServerGame::hasGameStarted() const
{
	return gameStarted;
}



Uint16 YOGServerGame::getHostPlayerID() const
{
	return host->getPlayerID();
}



void YOGServerGame::chooseLatencyMode()
{
	latencyUpdateTimer=SDL_GetTicks();
	
	unsigned highest = 0;
	unsigned second_highest = 0;
	for(unsigned i=0; i<players.size(); ++i)
	{
		unsigned p = players[i]->getAveragePing();
		if(p > highest)
		{
			second_highest = highest;
			highest = p;
		}
		else if(p > second_highest)
		{
			second_highest = p;
		}
	}

	//Add 5% to both pings. The given pings are such that 99.7% of all pings will
	//be under those amounts, provided pings are normally distributed
	int total_allocation = (highest * 105 + second_highest * 105) / 100;
	int latency_adjustment = std::min(255, (total_allocation+39) / 40);

	if(latency_adjustment != latencyMode && !gameStarted)
	{
		boost::shared_ptr<NetSetLatencyMode> message(new NetSetLatencyMode(latency_adjustment));
		routeMessage(message);
		latencyMode = latency_adjustment;
	}
}


void YOGServerGame::setPlayerGameResult(boost::shared_ptr<YOGServerPlayer> sender, YOGGameResult result)
{
	if(gameResults.getGameResultState(sender->getPlayerName()) == YOGGameResultUnknown)
	{
		gameResults.setGameResultState(sender->getPlayerName(), result);
	}
}



void YOGServerGame::sendGameResultsToGameLog()
{
	if(gameStarted)
	{
		server.getGameLog().addGameResults(gameResults);
		server.getPlayerScoreCalculator().proccessResults(gameResults, gameHeader);
	}
}


const std::string YOGServerGame::getRouterIP() const
{
	return routerIP;
}


Uint16 YOGServerGame::getFileID() const
{
	return mapFile;
}


