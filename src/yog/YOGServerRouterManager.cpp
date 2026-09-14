// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerRouterManager.h"
#include "YOGServer.h"
#include "NetConnection.h"
#include "RouterMessages.h"

using std::static_pointer_cast;

YOGServerRouterManager::YOGServerRouterManager(YOGServer& /*server*/)
	: listener(YOG_SERVER_ROUTER_PORT)
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
			//This receives the router information
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
	if (routers.empty()) return {};
	n %= routers.size();
	auto selected = routers[n];
	n = (n + 1) % routers.size();
	return selected;
}

bool YOGServerRouterManager::hasRouter() const
{
	return !routers.empty();
}
