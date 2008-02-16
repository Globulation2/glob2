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

#include "YOGGame.h"
#include <algorithm>
#include "YOGMapDistributor.h"
#include "YOGGameServer.h"

YOGGame::YOGGame(Uint16 gameID, Uint32 chatChannel, YOGGameServer& server)
	: gameID(gameID), chatChannel(chatChannel), server(server), playerManager(gameHeader)
{
	requested=false;
	gameStarted=false;
	oldReadyToLaunch=false;
	latencyMode = 0;
	latencyUpdateTimer = 1000;
}


void YOGGame::update()
{
	latencyUpdateTimer -= 1;
	if(latencyUpdateTimer == 0)
	{
		chooseLatencyMode();
		latencyUpdateTimer=1000;
	}


	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end();)
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
			for(std::vector<shared_ptr<YOGPlayer> >::iterator j = players.begin(); j!=players.end(); ++j)
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
	if(distributor)
		distributor->update();
	if(gameStarted == false)
	{
		if(playerManager.isEveryoneReadyToGo() && !oldReadyToLaunch)
		{
			shared_ptr<NetEveryoneReadyToLaunch> readyToLaunch(new NetEveryoneReadyToLaunch);
			host->sendMessage(readyToLaunch);
			oldReadyToLaunch=true;
		}
		else if(!playerManager.isEveryoneReadyToGo() && oldReadyToLaunch)
		{
			shared_ptr<NetNotEveryoneReadyToLaunch> notReadyToLaunch(new NetNotEveryoneReadyToLaunch);
			host->sendMessage(notReadyToLaunch);
			oldReadyToLaunch=false;
		}
	}
}

void YOGGame::addPlayer(shared_ptr<YOGPlayer> player)
{
	if(players.size()==0)
	{
		setHost(player);
	}
	else
	{
		shared_ptr<NetSendMapHeader> header1(new NetSendMapHeader(mapHeader));
		shared_ptr<NetSendGameHeader> header2(new NetSendGameHeader(gameHeader));
		shared_ptr<NetSendGamePlayerInfo> sendGamePlayerInfo(new NetSendGamePlayerInfo(gameHeader));
		player->sendMessage(header1);
		player->sendMessage(header2);
		player->sendMessage(sendGamePlayerInfo);
	}
	players.push_back(player);
	//Add the player to the chat channel for communication
	server.getChatChannelManager().getChannel(chatChannel)->addPlayer(player);
	playerManager.addPerson(player->getPlayerID(), player->getPlayerName());

	shared_ptr<NetPlayerJoinsGame> sendGamePlayerInfo(new NetPlayerJoinsGame(player->getPlayerID(), player->getPlayerName()));
	routeMessage(sendGamePlayerInfo);

	chooseLatencyMode();
}



void YOGGame::addAIPlayer(AI::ImplementitionID type)
{
	playerManager.addAIPlayer(type);

	shared_ptr<NetAddAI> addAI(new NetAddAI(static_cast<Uint8>(type)));
	routeMessage(addAI, host);
}



void YOGGame::removePlayer(shared_ptr<YOGPlayer> player)
{
	std::vector<shared_ptr<YOGPlayer> >::iterator i = std::find(players.begin(), players.end(), player);
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
			for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end();)
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

	//Remove the player from the chat channel
	server.getChatChannelManager().getChannel(chatChannel)->removePlayer(player);

	shared_ptr<NetSendGamePlayerInfo> sendGamePlayerInfo(new NetSendGamePlayerInfo(gameHeader));
	routeMessage(sendGamePlayerInfo);

	chooseLatencyMode();
}



void YOGGame::removeAIPlayer(int playerNum)
{
	playerManager.removePlayer(playerNum);

	shared_ptr<NetRemoveAI> removeAI(new NetRemoveAI(playerNum));
	routeMessage(removeAI, host);
}



void YOGGame::setTeam(int playerNum, int teamNum)
{
	playerManager.changeTeamNumber(playerNum, teamNum);

	shared_ptr<NetChangePlayersTeam> changeTeam(new NetChangePlayersTeam(playerNum, teamNum));
	routeMessage(changeTeam, host);
}



void YOGGame::setHost(shared_ptr<YOGPlayer> player)
{
	host = player;
}




void YOGGame::setMapHeader(const MapHeader& nmapHeader)
{
	mapHeader = nmapHeader;
	playerManager.setNumberOfTeams(mapHeader.getNumberOfTeams());
}



GameHeader& YOGGame::getGameHeader()
{
	return gameHeader;
}



void YOGGame::routeMessage(shared_ptr<NetMessage> message, shared_ptr<YOGPlayer> sender)
{
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i) != sender)
			(*i)->sendMessage(message);
	}
}



void YOGGame::routeOrder(shared_ptr<NetSendOrder> order, shared_ptr<YOGPlayer> sender)
{
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i) != sender)
			(*i)->sendMessage(order);
	}
}



shared_ptr<YOGMapDistributor> YOGGame::getMapDistributor()
{
	if(!distributor)
	{
		//clever trick to get a shared_ptr to this
		distributor.reset(new YOGMapDistributor(host->getGame(), host));
	}
	return distributor;
}



void YOGGame::kickPlayer(shared_ptr<NetKickPlayer> message)
{
	routeMessage(message, host);	
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i)->getPlayerID() == message->getPlayerID())
		{
			removePlayer(*i);
			break;
		}
	}
}



bool YOGGame::isEmpty() const
{
	return players.empty();
}



Uint16 YOGGame::getGameID() const
{
	return gameID;
}



void YOGGame::setReadyToStart(int playerID)
{
	playerManager.setReadyToGo(playerID, true);
}



void YOGGame::setNotReadyToStart(int playerID)
{
	playerManager.setReadyToGo(playerID, false);
}



void YOGGame::recieveGameStartRequest()
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



void YOGGame::startGame()
{
	gameStarted=true;
	boost::shared_ptr<NetStartGame> message(new NetStartGame);
	routeMessage(message);
	server.getGameInfo(gameID).setGameState(YOGGameInfo::GameRunning);
}



Uint32 YOGGame::getChatChannel() const
{
	return chatChannel;
}



bool YOGGame::hasGameStarted() const
{
	return gameStarted;
}



Uint16 YOGGame::getHostPlayerID() const
{
	return host->getPlayerID();
}



void YOGGame::chooseLatencyMode()
{
	int highest = 0;
	int second_highest = 0;
	for(int i=0; i<players.size(); ++i)
	{
		if(players[i]->getAveragePing() > highest)
		{
			second_highest = highest;
			highest = players[i]->getAveragePing();
		}
		else if(players[i]->getAveragePing() > second_highest)
		{
			second_highest = players[i]->getAveragePing();
		}
	}

	int total_allocation = (highest * 12 + second_highest * 12) / 10;
	int latency_adjustment = 0;
	if(total_allocation < 320)
		latency_adjustment = 8;
	else if(total_allocation < 540)
		latency_adjustment = 14;
	else if(total_allocation < 800)
		latency_adjustment = 20;

	if(latency_adjustment != latencyMode && !gameStarted)
	{
		boost::shared_ptr<NetSetLatencyMode> message(new NetSetLatencyMode(latency_adjustment));
		routeMessage(message);
		latencyMode = latency_adjustment;
	}
}



