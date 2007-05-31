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
#include <algorithm>

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
		Uint16 id = chooseNewPlayerID();
		players[id]=shared_ptr<YOGPlayer>(new YOGPlayer(nc, id, *this));
		nc.reset(new NetConnection);
	}

	//Call update to all of the players
	for(std::map<Uint16, shared_ptr<YOGPlayer> >::iterator i=players.begin(); i!=players.end(); ++i)
	{
		i->second->update();
	}

	//Remove all of the players that have disconnected.
	for(std::map<Uint16, shared_ptr<YOGPlayer> >::iterator i=players.begin(); i!=players.end();)
	{
		if(!i->second->isConnected())
		{
			playerHasLoggedOut(i->second->getPlayerID());
			std::map<Uint16, shared_ptr<YOGPlayer> >::iterator to_erase=i;
			i++;
			players.erase(to_erase);
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
	while(nl.isListening())
	{
		const int speed = 4;
		int startTick, endTick;
		startTick = SDL_GetTicks();
		update();
		endTick=SDL_GetTicks();
		int remaining = std::max(speed - endTick + startTick, 0);
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
	return gameList;
}

	
const std::list<YOGPlayerInfo>& YOGGameServer::getPlayerList() const
{
	return playerList;
}



void YOGGameServer::propogateMessage(boost::shared_ptr<YOGMessage> message)
{
	///unimplemented
}



void YOGGameServer::playerHasLoggedIn(const std::string& username, Uint16 id)
{
	playerList.push_back(YOGPlayerInfo(username, id));
}



void YOGGameServer::playerHasLoggedOut(Uint16 playerID)
{
	for(std::list<YOGPlayerInfo>::iterator i=playerList.begin(); i!=playerList.end(); ++i)
	{
		if(i->getPlayerID() == playerID)
		{
			playerList.erase(i);
			break;
		}
	}
}



YOGGameCreateRefusalReason YOGGameServer::canCreateNewGame(const std::string& game)
{
	//not implemented
	return YOGCreateRefusalUnknown;
}




Uint16 YOGGameServer::createNewGame(const std::string& name)
{
	//choose the new game ID
	Uint16 newID=1;
	while(true)
	{
		bool found=false;
		for(std::list<YOGGameInfo>::iterator i=gameList.begin(); i!=gameList.end(); ++i)
		{
			if(i->getGameID() == newID)
			{
				found=true;
				break;
			}
		}
		if(found)
			newID+=1;
		else
			break;
	}
	gameList.push_back(YOGGameInfo(name, newID));
	games[newID] = shared_ptr<YOGGame>(new YOGGame(newID));
	return newID;
}


YOGGameJoinRefusalReason YOGGameServer::canJoinGame(Uint16 gameID)
{
	//not implemented
	return YOGJoinRefusalUnknown;
}



shared_ptr<YOGGame> YOGGameServer::getGame(Uint16 gameID)
{
	return games[gameID];
}



shared_ptr<YOGPlayer> YOGGameServer::getPlayer(Uint16 playerID)
{
	return players[playerID];
}



Uint16 YOGGameServer::chooseNewPlayerID()
{
	//choose the new player ID.
	Uint16 newID=1;
	while(true)
	{
		bool found=false;
		if(players.find(newID) != players.end())
			found=true;
		if(found)
			newID+=1;
		else
			break;
	}
	return newID;
}
