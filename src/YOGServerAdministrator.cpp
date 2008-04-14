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

#include "YOGServerAdministrator.h"
#include "YOGServerAdministratorCommands.h"
#include "YOGServer.h"
#include "YOGServerPlayer.h"
#include "YOGMessage.h"
#include "NetMessage.h"

YOGServerAdministrator::YOGServerAdministrator(YOGServer* server)
	: server(server)
{
	commands.push_back(new YOGServerRestart);
	commands.push_back(new YOGMutePlayer);
	commands.push_back(new YOGUnmutePlayer);
	commands.push_back(new YOGResetPassword);
	commands.push_back(new YOGBanPlayer);
	commands.push_back(new YOGUnbanPlayer);
	commands.push_back(new YOGShowBannedPlayers);
}



YOGServerAdministrator::~YOGServerAdministrator()
{
	for(int i=0; i<commands.size(); ++i)
	{
		delete commands[i];
	}
}

	

bool YOGServerAdministrator::executeAdministrativeCommand(const std::string& message, boost::shared_ptr<YOGServerPlayer> player)
{
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
		return false;
	}


	if(tokens[0] == ".help")
	{
		sendTextMessage("The current list of YOG Administrative Commands are: ", player);
		for(int i=0; i<commands.size(); ++i)
		{
			sendTextMessage(commands[i]->getHelpMessage(), player);
		}
		sendTextMessage(".help    Shows this help message", player);
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
					commands[i]->execute(server, this, tokens, player);
				}
			}
		}
	}
	return false;
}


void YOGServerAdministrator::sendTextMessage(const std::string& message, boost::shared_ptr<YOGServerPlayer> player)
{
	boost::shared_ptr<YOGMessage> m(new YOGMessage(message, "admin", YOGAdministratorMessage));
	boost::shared_ptr<NetSendYOGMessage> send(new NetSendYOGMessage(LOBBY_CHAT_CHANNEL, m));
	player->sendMessage(send);
}

