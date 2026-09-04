// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerRouterPlayer.h"
#include "NetConnection.h"
#include "NetMessage.h"
#include "YOGServerGameRouter.h"
#include "YOGServerRouter.h"

using std::static_pointer_cast;

YOGServerRouterPlayer::YOGServerRouterPlayer(std::shared_ptr<NetConnection> connection, YOGServerRouter* router)
	: connection(connection), router(router), isAdmin(false)
{
}



void YOGServerRouterPlayer::setPointer(std::weak_ptr<YOGServerRouterPlayer> npointer)
{
	pointer = npointer;
}



void YOGServerRouterPlayer::sendNetMessage(std::shared_ptr<NetMessage> message)
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
		//This recieves the client information
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
			game->addPlayer(std::shared_ptr<YOGServerRouterPlayer>(pointer));
		}
		else if(type==MNetRouterAdministratorLogin)
		{
			shared_ptr<NetRouterAdministratorLogin> info = static_pointer_cast<NetRouterAdministratorLogin>(message);
			std::string password = info->getPassword();
			if(router->isAdministratorPasswordCorrect(password))
			{
				isAdmin=true;
				std::shared_ptr<NetRouterAdministratorLoginAccepted> m = std::shared_ptr<NetRouterAdministratorLoginAccepted>(new NetRouterAdministratorLoginAccepted);
				sendNetMessage(m);
			}
			else
			{
				std::shared_ptr<NetRouterAdministratorLoginRefused> m = std::shared_ptr<NetRouterAdministratorLoginRefused>(new NetRouterAdministratorLoginRefused(YOGRouterLoginWrongPassword));
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

