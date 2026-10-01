// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "FileManager.h"
#include <iostream>
#include "NetConnection.h"
#include "RouterMessages.h"
#include "Stream.h"
#include "Toolkit.h"
#include "YOGConsts.h"
#include "YOGServerGameRouter.h"
#include "YOGServerRouter.h"
#include "YOGServerRouterPlayer.h"
#include "SDLCompat.h"
#include <sstream>
#include "ServerControl.h"
#include <chrono>

using namespace GAGCore;
using std::static_pointer_cast;

YOGServerRouter::YOGServerRouter() : YOGServerRouter(makeNetworkConfig(false, true)) {}
YOGServerRouter::YOGServerRouter(const std::string& endpoint) : YOGServerRouter([&] {
    auto config = makeNetworkConfig(false, true); config.registrationEndpoint = endpoint; return config;
}()) {}
YOGServerRouter::YOGServerRouter(const NetworkConfig& config)
    : configuration(config), nl(config.router), admin(this)
{
    new_connection = std::make_shared<NetConnection>();
    yog_connection = std::make_shared<NetConnection>(makeNetTransport(config.registration.tls));
    yog_connection->openConnection(config.registrationEndpoint, 0);
    shutdownMode = false;
}

void YOGServerRouter::update()
{
	//First attempt connections with new players
	while(!shutdownMode && nl.attemptConnection(*new_connection))
	{
		players.push_back(shared_ptr<YOGServerRouterPlayer>(new YOGServerRouterPlayer(new_connection, this)));
		players[players.size()-1]->setPointer(players[players.size()-1]);
		new_connection.reset(new NetConnection);
	}
	
	//Call update to all of the players
	for(std::vector<std::shared_ptr<YOGServerRouterPlayer> >::iterator i=players.begin(); i!=players.end(); ++i)
	{
		(*i)->update();
	}
	
	//Call update to all of the games
	for(std::map<Uint16, shared_ptr<YOGServerGameRouter> >::iterator i=games.begin(); i!=games.end(); ++i)
	{
		i->second->update();
	}

	//Removes all players that have disconnected
	for(std::vector<std::shared_ptr<YOGServerRouterPlayer> >::iterator i = players.begin(); i!=players.end();)
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
	
	
	if (yog_connection->isConnected() && !registrationSent && !shutdownMode) {
        yog_connection->sendMessage(std::make_shared<NetRegisterRouter>()); registrationSent = true;
    }
    //Parse incoming messages.
	shared_ptr<NetMessage> message = yog_connection->getMessage();
	if(message)
	{
		Uint8 type = message->getMessageType();
		//This receives the client information
		if(type==MNetAcknowledgeRouter)
		{
            registrationConfirmed = true;
		}
	}
	if(!yog_connection->isConnected() && !yog_connection->isConnecting() && !shutdownMode)
	{
		std::cout<<"Router lost connection."<<std::endl;
		enterShutdownMode();
	}
}



int YOGServerRouter::run()
{
    ServerControl::installSignals();
    ServerControl control(configuration.controlBind, configuration.controlPort);
    std::chrono::steady_clock::time_point drainStarted{};
	std::cout<<"Router started successfully."<<std::endl;
	while(nl.isListening() || shutdownMode)
	{
		const int speed = 25;
		Uint64 startTick, endTick;
		startTick = SDL_GetTicks64();
        if (ServerControl::shutdownRequested() && !shutdownMode) enterShutdownMode();
        update();
        if (shutdownMode && drainStarted == std::chrono::steady_clock::time_point{})
            drainStarted = std::chrono::steady_clock::now();
        control.update({!shutdownMode && registrationConfirmed && yog_connection->isConnected(), shutdownMode, players.size(), games.size()});
        if (shutdownMode && std::chrono::steady_clock::now() - drainStarted >= std::chrono::seconds(configuration.drainSeconds)) {
            std::cerr << "Router drain deadline expired; active games will be interrupted" << std::endl; break;
        }
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



std::shared_ptr<YOGServerGameRouter> YOGServerRouter::getGame(Uint16 gameID)
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
    nl.stopListening();
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


