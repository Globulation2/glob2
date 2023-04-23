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

#include <iostream>
#include <istream>
#include "NetConnection.h"
#include "NetMessage.h"
#include "YOGClientRouterAdministrator.h"
#include "YOGConsts.h"

using boost::static_pointer_cast;
	
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
	
	boost::shared_ptr<NetRouterAdministratorLogin> login(new NetRouterAdministratorLogin(password));
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
		boost::shared_ptr<NetRouterAdministratorLoginRefused> info = static_pointer_cast<NetRouterAdministratorLoginRefused>(message);
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
		boost::shared_ptr<NetRouterAdministratorSendCommand> cmd(new NetRouterAdministratorSendCommand(command));
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
				boost::shared_ptr<NetRouterAdministratorSendText> info = static_pointer_cast<NetRouterAdministratorSendText>(message);
				std::cout<<info->getText()<<std::endl;
			}
			message = connect.getMessage();
		}
		SDL_Delay(50);
	}
	
	return 0;
}



