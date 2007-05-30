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
	players.push_back(player);
	for(int i=0; i<32; ++i)
	{
		if(gameHeader.getBasePlayer(i).type == BasePlayer::P_NONE)
		{
			gameHeader.getBasePlayer(i) = BasePlayer(i, player->getPlayerName().c_str(), i, BasePlayer::P_IP);
		}
	}
	sendGameHeaderPlayers();
}



void YOGGame::removePlayer(shared_ptr<YOGPlayer> player)
{
	std::vector<shared_ptr<YOGPlayer> >::iterator i = std::find(players.begin(), players.end(), player);
	if(i!=players.end())
		players.erase(i);
}




void YOGGame::setMapHeader(const MapHeader& nmapHeader)
{
	mapHeader = nmapHeader;
}




void YOGGame::routeMessage(shared_ptr<NetMessage> message)
{
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		(*i)->sendMessage(message);
	}
}



void YOGGame::sendGameHeaderPlayers()
{
	shared_ptr<NetUpdateGameHeaderPlayers> message(new NetUpdateGameHeaderPlayers(gameHeader));
	for(std::vector<shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		(*i)->sendMessage(message);
	}
}
