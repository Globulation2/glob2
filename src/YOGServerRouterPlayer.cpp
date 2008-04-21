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


#include "YOGServerRouterPlayer.h"
#include "NetConnection.h"
#include "NetMessage.h"
#include "YOGServerGameRouter.h"
#include "YOGServerRouter.h"


YOGServerRouterPlayer::YOGServerRouterPlayer(boost::shared_ptr<NetConnection> connection, YOGServerRouter* router)
	: connection(connection), router(router)
{
	
}



void YOGServerRouterPlayer::setPointer(boost::weak_ptr<YOGServerRouterPlayer> npointer)
{
	pointer = npointer;
}



void YOGServerRouterPlayer::sendNetMessage(boost::shared_ptr<NetMessage> message)
{
	connection->sendMessage(message);
}



void YOGServerRouterPlayer::update()
{
	connection->update();
	//Parse incoming messages
	shared_ptr<NetMessage> message = connection->getMessage();
	if(!message)
		return;
	Uint8 type = message->getMessageType();
	//This recieves the client information
	if(type==MNetSendOrder)
	{
		shared_ptr<NetSendOrder> info = static_pointer_cast<NetSendOrder>(message);
		if(game)
		{
			game->routeMessage(message, this);
		}
	}
	if(type==MNetSetGameInRouter)
	{
		shared_ptr<NetSetGameInRouter> info = static_pointer_cast<NetSetGameInRouter>(message);
		game = router->getGame(info->getGameID());
		game->addPlayer(boost::shared_ptr<YOGServerRouterPlayer>(pointer));
	}
}


bool YOGServerRouterPlayer::isConnected()
{
	return connection->isConnected();
}

