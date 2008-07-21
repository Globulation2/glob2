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


#include <string>
#include "boost/shared_ptr.hpp"
#include <vector>
#include "NetMessage.h"
#include "YOGServerRouterAdministrator.h"
#include "YOGServerRouterAdministratorCommands.h"
#include "YOGServerRouterPlayer.h"


YOGServerRouterAdministrator::YOGServerRouterAdministrator(YOGServerRouter* router)
	: router(router)
{
	commands.push_back(new YOGServerRouterShutdownCommand);
}



YOGServerRouterAdministrator::~YOGServerRouterAdministrator()
{
	for(int i=0; i<commands.size(); ++i)
	{
		delete commands[i];
	}
}



bool YOGServerRouterAdministrator::executeAdministrativeCommand(const std::string& message, YOGServerRouterPlayer* player)
{
	allText.clear();
	std::vector<std::string> tokens;
	std::string token;
	bool isQuotes=false;
	for(int i=0; i<message.size(); ++i)
	{
		if(message[i]==' ' && !isQuotes)
		{
			if(!token.empty())
			{
				tokens.push_back(token);
				token.clear();
			}
		}
		else if(message[i]=='"' && isQuotes)
		{
			isQuotes=false;
		}
		else if(message[i]=='"' && !isQuotes)
		{
			isQuotes=true;
		}
		else
		{
			token+=message[i];
		}
	}
	if(!token.empty())
	{
		tokens.push_back(token);
		token.clear();
	}
	
	if(tokens.size() == 0)
	{
		sendTextMessage("Use help to get a list of commands", player);
		flushTexts(player);
		return false;
	}


	if(tokens[0] == "help")
	{
		sendTextMessage("The current list of YOG Router Administrative Commands are: ", player);
		for(int i=0; i<commands.size(); ++i)
		{
			sendTextMessage(commands[i]->getHelpMessage(), player);
		}
		sendTextMessage("help    Shows this help message", player);
		flushTexts(player);
		return true;
	}
	else
	{
		for(int i=0; i<commands.size(); ++i)
		{
			if(tokens[0] == commands[i]->getCommandName())
			{
				if(!commands[i]->doesMatch(tokens))
				{
					sendTextMessage(commands[i]->getHelpMessage(), player);
				}
				else
				{
					commands[i]->execute(router, this, tokens, player);
				}
				flushTexts(player);
				return true;
			}
		}
	}
	sendTextMessage("Use help to get a list of commands", player);
	flushTexts(player);
	return false;
}



void YOGServerRouterAdministrator::sendTextMessage(const std::string& message, YOGServerRouterPlayer* admin)
{
	allText += message;
	allText += "\n";
}



void YOGServerRouterAdministrator::flushTexts(YOGServerRouterPlayer* admin)
{
	boost::shared_ptr<NetRouterAdministratorSendText> text(new NetRouterAdministratorSendText(allText));
	admin->sendNetMessage(text);
	allText.clear();
}

