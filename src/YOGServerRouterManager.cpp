// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerRouterManager.h"
#include "YOGServer.h"
#include "NetConnection.h"
#include "NetMessage.h"

using std::static_pointer_cast;

YOGServerRouterManager::YOGServerRouterManager(YOGServer& server)
	: listener(YOG_SERVER_ROUTER_PORT), server(server)
{
	new_connection.reset(new NetConnection);
	n=0;
}



void YOGServerRouterManager::addRouter(std::shared_ptr<NetConnection> connection)
{
	shared_ptr<NetAcknowledgeRouter> info(new NetAcknowledgeRouter);
	connection->sendMessage(info);
	routers.push_back(connection);
}


void YOGServerRouterManager::update()
{
	//First attempt connections with new routers
	while(listener.attemptConnection(*new_connection))
	{
		addRouter(new_connection);
		new_connection.reset(new NetConnection);
	}

	//Update all routers
	for(std::vector<std::shared_ptr<NetConnection> >::iterator i = routers.begin(); i!=routers.end(); ++i)
	{
		(*i)->update();
		//Parse incoming messages.
		shared_ptr<NetMessage> message = (*i)->getMessage();
		if(message)
		{
			Uint8 type = message->getMessageType();
			//This recieves the router information
			if(type==MNetRegisterRouter)
			{
				shared_ptr<NetRegisterRouter> info = static_pointer_cast<NetRegisterRouter>(message);
			}
		}
	}
	
	for(std::vector<std::shared_ptr<NetConnection> >::iterator i = routers.begin(); i!=routers.end();)
	{
		if(!(*i)->isConnected())
		{
			Uint32 n = i - routers.begin();
			routers.erase(i);
			i = routers.begin() + n;
		}
		else
		{
			++i;
		}
	}
}


std::shared_ptr<NetConnection> YOGServerRouterManager::chooseYOGRouter()
{
	n+=1;
	if(n == (int)routers.size())
		n = 0;
	return routers[n];
}

