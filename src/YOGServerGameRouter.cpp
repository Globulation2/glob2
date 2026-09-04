// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerGameRouter.h"
#include "YOGServerRouterPlayer.h"
#include "NetMessage.h"


YOGServerGameRouter::YOGServerGameRouter()
{

}



void YOGServerGameRouter::addPlayer(std::shared_ptr<YOGServerRouterPlayer> player)
{
	players.push_back(player);
}



void YOGServerGameRouter::update()
{
	for(std::vector<std::shared_ptr<YOGServerRouterPlayer> >::iterator i=players.begin(); i!=players.end();)
	{
		if(!(*i)->isConnected())
		{
			Uint32 n = i - players.begin();
			players.erase(i);
			i = players.begin() + n;
		}
		else
		{
			++i;
		}
	}
}



bool YOGServerGameRouter::isEmpty()
{
	if(players.empty())
		return true;
	return false;
}



void YOGServerGameRouter::routeMessage(std::shared_ptr<NetMessage> message, YOGServerRouterPlayer* sender)
{
	for(std::vector<std::shared_ptr<YOGServerRouterPlayer> >::iterator i=players.begin(); i!=players.end(); ++i)
	{
		if(i->get() != sender)
		{
			(*i)->sendNetMessage(message);
		}
	}
}

