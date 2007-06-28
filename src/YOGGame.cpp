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

#include "YOGGame.h"
#include <algorithm>
#include "YOGMapDistributor.h"


YOGGame::YOGGame(Uint16 gameID)
	: gameID(gameID)
{
	requested=false;
	gameStarted=false;
}


void YOGGame::update()
{
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end();)
	{
		if(!(*i)->isConnected())
		{
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
		player->sendMessage(header1);
		player->sendMessage(header2);
	}
	players.push_back(player);
	shared_ptr<NetPlayerJoinsGame> join(new NetPlayerJoinsGame(player->getPlayerID()));
	routeMessage(join);
}



void YOGGame::removePlayer(shared_ptr<YOGPlayer> player)
{
	std::vector<shared_ptr<YOGPlayer> >::iterator i = std::find(players.begin(), players.end(), player);
	if(i!=players.end())
		players.erase(i);
	if(player!=host || gameStarted)
	{
		for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
		{
			shared_ptr<NetPlayerLeavesGame> message(new NetPlayerLeavesGame(player->getPlayerID()));
			(*i)->sendMessage(message);
		}
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



void YOGGame::setHost(shared_ptr<YOGPlayer> player)
{
	host = player;
}




void YOGGame::setMapHeader(const MapHeader& nmapHeader)
{
	mapHeader = nmapHeader;
}




void YOGGame::setGameHeader(const GameHeader& nGameHeader)
{
	gameHeader = nGameHeader;
	shared_ptr<NetSendGameHeader> message(new NetSendGameHeader(gameHeader));
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i) != host)
			(*i)->sendMessage(message);
	}
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



void YOGGame::sendKickMessage(shared_ptr<NetKickPlayer> message)
{
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if((*i)->getPlayerID() == message->getPlayerID())
			(*i)->sendMessage(message);
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



void YOGGame::sendReadyToStart(shared_ptr<NetReadyToLaunch> message)
{
	host->sendMessage(message);
}



void YOGGame::sendNotReadyToStart(shared_ptr<NetNotReadyToLaunch> message)
{
	host->sendMessage(message);
}



void YOGGame::startGame()
{
	gameStarted=true;
	boost::shared_ptr<NetStartGame> message(new NetStartGame);
	routeMessage(message, host);
}


