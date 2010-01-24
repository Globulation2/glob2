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

#include "YOGServerRouterManager.h"
#include "YOGServer.h"
#include "NetConnection.h"
#include "NetMessage.h"

YOGServerRouterManager::YOGServerRouterManager(YOGServer& server)
	: listener(YOG_SERVER_ROUTER_PORT), server(server)
{
	new_connection.reset(new NetConnection);
	n=0;
}



void YOGServerRouterManager::addRouter(boost::shared_ptr<NetConnection> connection)
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
	for(std::vector<boost::shared_ptr<NetConnection> >::iterator i = routers.begin(); i!=routers.end(); ++i)
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
	
	for(std::vector<boost::shared_ptr<NetConnection> >::iterator i = routers.begin(); i!=routers.end();)
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


boost::shared_ptr<NetConnection> YOGServerRouterManager::chooseYOGRouter()
{
	n+=1;
	if(n == (int)routers.size())
		n = 0;
	return routers[n];
}

