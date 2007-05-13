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

#include "YOGGameServer.h"
#include "NetTestSuite.h"

YOGGameServer::YOGGameServer(YOGLoginPolicy loginPolicy, YOGGamePolicy gamePolicy)
	: loginPolicy(loginPolicy), gamePolicy(gamePolicy)
{
	nl.startListening(YOG_SERVER_PORT);
}



void YOGGameServer::update()
{
	//First attempt connections with new players
	shared_ptr<NetConnection> nc(new NetConnection);
	while(nl.attemptConnection(*nc))
	{
		players.push_back(shared_ptr<YOGPlayer>(new YOGPlayer(nc)));
		nc.reset(new NetConnection);
	}

	//Call update to all of the players
	for(std::list<shared_ptr<YOGPlayer> >::iterator i=players.begin(); i!=players.end(); ++i)
	{
		(*i)->update(*this);
	}

	//Remove all of the players that have disconnected.
	for(std::list<shared_ptr<YOGPlayer> >::iterator i=players.begin(); i!=players.end();)
	{
		if(!(*i)->isConnected())
		{
			i = players.erase(i);
		}
		else
		{
			i++;
		}
	}
}



int YOGGameServer::run()
{
	NetTestSuite tests;
	bool cont = tests.runAllTests();
	if(!cont)
		return 1;
	while(true)
	{
		const Uint32 speed = 25;
		Uint32 startTick, endTick;
		startTick = SDL_GetTicks();
		update();
		endTick=SDL_GetTicks();
		Uint32 remaining = speed - endTick + startTick;
		SDL_Delay(remaining);
	}
	return 0;
}



YOGLoginPolicy YOGGameServer::getLoginPolicy() const
{
	return loginPolicy;
}



YOGGamePolicy YOGGameServer::getGamePolicy() const
{
	return gamePolicy;
}



YOGLoginState YOGGameServer::verifyLoginInformation(const std::string& username, const std::string& password)
{
	///Doesn't do anything yet.
	return YOGLoginSuccessful;
}


const std::list<YOGGameInfo>& YOGGameServer::getGameList() const
{
	return gameInfos;
}

	
const std::list<YOGPlayerInfo>& YOGGameServer::getPlayerList() const
{
	return playerList;
}


