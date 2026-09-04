// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include <iostream>
#include <istream>
#include "NetConnection.h"
#include "NetMessage.h"
#include "YOGClientRouterAdministrator.h"
#include "YOGConsts.h"

using std::static_pointer_cast;
	
YOGClientRouterAdministrator::YOGClientRouterAdministrator()
{

}



int YOGClientRouterAdministrator::execute()
{
	std::cout<<"IP Address of YOG router? "<<std::flush;
	std::string ip;
	std::cin>>ip;
	if(std::cin.eof())
	{
		std::cout<<std::endl;
		return 0;
	}
	std::cout<<"Password for YOG router? "<<std::flush;
	std::string password;
	std::cin>>password;
	if(std::cin.eof())
	{
		std::cout<<std::endl;
		return 0;
	}
	
	std::cout<<"Connecting"<<std::endl;
	NetConnection connect(ip, YOG_ROUTER_PORT);
	while(connect.isConnecting())
	{
		connect.update();
		SDL_Delay(50);
	}
	if(connect.isConnected())
	{
		std::cout<<"Connected"<<std::endl;
	}
	else
	{
		std::cout<<"Connection to "<<ip<<" failed."<<std::endl;
		return 1;
	}
	
	std::shared_ptr<NetRouterAdministratorLogin> login(new NetRouterAdministratorLogin(password));
	connect.sendMessage(login);
	
	//Parse incoming messages and generate events
	shared_ptr<NetMessage> message = connect.getMessage();
	while(!message)
	{
		connect.update();
		message = connect.getMessage();
		SDL_Delay(50);
	}
	Uint8 type = message->getMessageType();
	if(type != MNetRouterAdministratorLoginAccepted && type != MNetRouterAdministratorLoginRefused)
	{
		std::cout<<"Router version is incompatible. Please update your version and/or contact the router administrator."<<std::endl;
		return 2;
	}
	
	if(type == MNetRouterAdministratorLoginRefused)
	{
		std::shared_ptr<NetRouterAdministratorLoginRefused> info = static_pointer_cast<NetRouterAdministratorLoginRefused>(message);
		YOGRouterAdministratorLoginRefusalReason reason = info->getReason();
		if(reason == YOGRouterLoginWrongPassword)
		{
			std::cout<<"Router has refused administrative login because the password is wrong."<<std::endl;
			return 3;
		}
		else if(reason == YOGRouterLoginUnknown)
		{
			std::cout<<"Router has refused administrative login for an unknown reason."<<std::endl;
			return 4;
		}
	}
	
	while(!std::cin.eof() && connect.isConnected())
	{
		std::cout<<"> "<<std::flush;
		std::string command;
		std::cin>>command;
		if(std::cin.eof())
		{
			std::cout<<std::endl;
			return 0;
		}
		std::shared_ptr<NetRouterAdministratorSendCommand> cmd(new NetRouterAdministratorSendCommand(command));
		connect.sendMessage(cmd);
		
		//Parse incoming messages and generate events
		message = connect.getMessage();
		while(!message && connect.isConnected())
		{
			connect.update();
			SDL_Delay(50);
			message = connect.getMessage();
		}
		
		while(message)
		{
			Uint8 type = message->getMessageType();
			if(type == MNetRouterAdministratorSendText)
			{
				std::shared_ptr<NetRouterAdministratorSendText> info = static_pointer_cast<NetRouterAdministratorSendText>(message);
				std::cout<<info->getText()<<std::endl;
			}
			message = connect.getMessage();
		}
		SDL_Delay(50);
	}
	
	return 0;
}



