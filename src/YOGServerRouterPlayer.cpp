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

using boost::static_pointer_cast;

YOGServerRouterPlayer::YOGServerRouterPlayer(boost::shared_ptr<NetConnection> connection, YOGServerRouter* router)
	: connection(connection), router(router), isAdmin(false)
{
}



void YOGServerRouterPlayer::setPointer(boost::weak_ptr<YOGServerRouterPlayer> pointer)
{
	this->pointer = pointer;
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
	while(message)
	{
		Uint8 type = message->getMessageType();
		//This receives the client information
		if(type==MNetSendOrder)
		{
			shared_ptr<NetSendOrder> info = static_pointer_cast<NetSendOrder>(message);
			if(game)
			{
				game->routeMessage(message, this);
			}
		}
		else if(type==MNetSetGameInRouter)
		{
			shared_ptr<NetSetGameInRouter> info = static_pointer_cast<NetSetGameInRouter>(message);
			game = router->getGame(info->getGameID());
			game->addPlayer(boost::shared_ptr<YOGServerRouterPlayer>(pointer));
		}
		else if(type==MNetRouterAdministratorLogin)
		{
			shared_ptr<NetRouterAdministratorLogin> info = static_pointer_cast<NetRouterAdministratorLogin>(message);
			std::string password = info->getPassword();
			if(router->isAdministratorPasswordCorrect(password))
			{
				isAdmin=true;
				boost::shared_ptr<NetRouterAdministratorLoginAccepted> m = boost::shared_ptr<NetRouterAdministratorLoginAccepted>(new NetRouterAdministratorLoginAccepted);
				sendNetMessage(m);
			}
			else
			{
				boost::shared_ptr<NetRouterAdministratorLoginRefused> m = boost::shared_ptr<NetRouterAdministratorLoginRefused>(new NetRouterAdministratorLoginRefused(YOGRouterLoginWrongPassword));
				sendNetMessage(m);
			}
		}
		else if(type==MNetRouterAdministratorSendCommand)
		{
			shared_ptr<NetRouterAdministratorSendCommand> info = static_pointer_cast<NetRouterAdministratorSendCommand>(message);
			std::string command = info->getCommand();
			if(isAdmin)
			{
				router->getAdministrator().executeAdministrativeCommand(command, this);
			}
		}
		message = connection->getMessage();
	}
}


bool YOGServerRouterPlayer::isConnected()
{
	return connection->isConnected();
}


bool YOGServerRouterPlayer::isAdministrator()
{
	return isAdmin;
}

