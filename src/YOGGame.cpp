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

YOGGame::YOGGame(Uint16 gameID)
	: gameID(gameID)
{

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
		shared_ptr<NetPlayerJoinsGame> join(new NetPlayerJoinsGame(player->getPlayerID()));
		host->sendMessage(join);
	}
	players.push_back(player);
}



void YOGGame::removePlayer(shared_ptr<YOGPlayer> player)
{
	std::vector<shared_ptr<YOGPlayer> >::iterator i = std::find(players.begin(), players.end(), player);
	if(i!=players.end())
		players.erase(i);
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
