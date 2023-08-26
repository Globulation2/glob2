/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "FileManager.h"
#include <iostream>
#include "NetConnection.h"
#include "NetMessage.h"
#include "Stream.h"
#include "Toolkit.h"
#include "YOGConsts.h"
#include "YOGServerGameRouter.h"
#include "YOGServerRouter.h"
#include "YOGServerRouterPlayer.h"
#include "SDLCompat.h"
#include <sstream>

using namespace GAGCore;
using boost::static_pointer_cast;

YOGServerRouter::YOGServerRouter()
	: nl(YOG_ROUTER_PORT), admin(this)
{
	new_connection.reset(new NetConnection);
	yog_connection.reset(new NetConnection(YOG_SERVER_IP, YOG_SERVER_ROUTER_PORT));
	shutdownMode=false;
}



YOGServerRouter::YOGServerRouter(const std::string& yogip)
	: nl(YOG_ROUTER_PORT), admin(this)
{
	new_connection.reset(new NetConnection);
	yog_connection.reset(new NetConnection(yogip, YOG_SERVER_ROUTER_PORT));
	shutdownMode=false;
}



void YOGServerRouter::update()
{
	//First attempt connections with new players
	while(nl.attemptConnection(*new_connection))
	{
		players.push_back(shared_ptr<YOGServerRouterPlayer>(new YOGServerRouterPlayer(new_connection, this)));
		players[players.size()-1]->setPointer(players[players.size()-1]);
		new_connection.reset(new NetConnection);
	}
	
	//Call update to all of the players
	for(std::vector<boost::shared_ptr<YOGServerRouterPlayer> >::iterator i=players.begin(); i!=players.end(); ++i)
	{
		(*i)->update();
	}
	
	//Call update to all of the games
	for(std::map<Uint16, shared_ptr<YOGServerGameRouter> >::iterator i=games.begin(); i!=games.end(); ++i)
	{
		i->second->update();
	}

	//Removes all players that have disconnected
	for(std::vector<boost::shared_ptr<YOGServerRouterPlayer> >::iterator i = players.begin(); i!=players.end();)
	{
		if(!(*i)->isConnected())
		{
			Uint32 n = i - players.begin();
			players.erase(i);
			i = players.begin() + n;
		}
		else
		{
			i++;
		}
	}
	
	//Remove old games
	for(std::map<Uint16, shared_ptr<YOGServerGameRouter> >::iterator i=games.begin(); i!=games.end();)
	{
		if(i->second->isEmpty())
		{
			std::map<Uint16, shared_ptr<YOGServerGameRouter> >::iterator to_erase=i;
			i++;
			games.erase(to_erase);
		}
		else
		{
			i++;
		}
	}
	
	
	//Parse incoming messages.
	shared_ptr<NetMessage> message = yog_connection->getMessage();
	if(message)
	{
		Uint8 type = message->getMessageType();
		//This recieves the client information
		if(type==MNetAcknowledgeRouter)
		{
			shared_ptr<NetAcknowledgeRouter> info = static_pointer_cast<NetAcknowledgeRouter>(message);
			shared_ptr<NetRegisterRouter> reg = shared_ptr<NetRegisterRouter>(new NetRegisterRouter);
			yog_connection->sendMessage(reg);
		}
	}
	if(!yog_connection->isConnected() && !yog_connection->isConnecting() && !shutdownMode)
	{
		std::cout<<"Router lost connection."<<std::endl;
		shutdownMode=true;
	}
}



int YOGServerRouter::run()
{
	std::cout<<"Router started successfully."<<std::endl;
	while(nl.isListening())
	{
		const int speed = 25;
		Uint64 startTick, endTick;
		startTick = SDL_GetTicks64();
		update();
		endTick=SDL_GetTicks64();
		int remaining = std::max<Sint64>(speed - static_cast<Sint64>(endTick) + static_cast<Sint64>(startTick), 0);
		SDL_Delay(remaining);
		
		if(shutdownMode)
		{
			if(games.size() == 0 && players.size() == 0)
				break;
		}
	}
	return 0;
}



boost::shared_ptr<YOGServerGameRouter> YOGServerRouter::getGame(Uint16 gameID)
{
	if(games.find(gameID) == games.end())
		games[gameID].reset(new YOGServerGameRouter);
	return games[gameID];
}


bool YOGServerRouter::isAdministratorPasswordCorrect(const std::string& password)
{
	InputLineStream* stream = new InputLineStream(Toolkit::getFileManager()->openInputStreamBackend(YOG_SERVER_FOLDER+"routerpassword.txt"));
	if(!stream->isEndOfStream())
	{
		std::string pass = stream->readLine();
		if(pass == password)
		{
			delete stream;
			return true;
		}
	}
	delete stream;
	return false;
}


YOGServerRouterAdministrator& YOGServerRouter::getAdministrator()
{
	return admin;
}


void YOGServerRouter::enterShutdownMode()
{
	shutdownMode=true;
	yog_connection->closeConnection();
}


std::string YOGServerRouter::getStatusReport()
{
	std::stringstream s;
	s<<"Status Report: "<<std::endl;
	s<<"\t"<<games.size()<<" active games"<<std::endl;
	s<<"\t"<<players.size()<<" connected players"<<std::endl;
	
	int count_admin=0;
	for(unsigned int i=0; i<players.size(); ++i)
	{
		if(players[i]->isAdministrator())
			count_admin+=1;
	}
	s<<"\t"<<count_admin<<" authenticed admins"<<std::endl;
	
	if(shutdownMode)
		s<<"\tServer is currently in shutdown mode"<<std::endl;
	else
		s<<"\tServer is currently in operating mode"<<std::endl;
	return s.str();
}


