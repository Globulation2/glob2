// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerAdministrator.h"
#include "YOGServerAdministratorCommands.h"
#include "YOGServer.h"
#include "YOGServerPlayer.h"
#include "YOGMessage.h"
#include "LobbyMessages.h"

YOGServerAdministrator::YOGServerAdministrator(YOGServer* server)
	: server(server)
{
	commands.push_back(std::make_unique<YOGServerRestart>());
	commands.push_back(std::make_unique<YOGMutePlayer>());
	commands.push_back(std::make_unique<YOGUnmutePlayer>());
	commands.push_back(std::make_unique<YOGResetPassword>());
	commands.push_back(std::make_unique<YOGBanPlayer>());
	commands.push_back(std::make_unique<YOGUnbanPlayer>());
	commands.push_back(std::make_unique<YOGShowBannedPlayers>());
	commands.push_back(std::make_unique<YOGBanIP>());
	commands.push_back(std::make_unique<YOGAddAdministrator>());
	commands.push_back(std::make_unique<YOGRemoveAdministrator>());
	commands.push_back(std::make_unique<YOGAddModerator>());
	commands.push_back(std::make_unique<YOGRemoveModerator>());
	commands.push_back(std::make_unique<YOGRemoveMap>());
}



YOGServerAdministrator::~YOGServerAdministrator() = default;

	

bool YOGServerAdministrator::executeAdministrativeCommand(const std::string& message, std::shared_ptr<YOGServerPlayer> player, bool moderator)
{
	std::vector<std::string> tokens;
	std::string token;
	bool isQuotes=false;
	for(unsigned int i=0; i<message.size(); ++i)
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
		if(moderator)
			sendTextMessage("The current list of YOG Administrative Commands available for moderators are: ", player);
		else
			sendTextMessage("The current list of YOG Administrative Commands are: ", player);
		for(unsigned int i=0; i<commands.size(); ++i)
		{
			if(!moderator || commands[i]->allowedForModerator())
			{
				sendTextMessage(commands[i]->getHelpMessage(), player);
			}
		}
		sendTextMessage(".help    Shows this help message", player);
	}
	else
	{
		for(unsigned int i=0; i<commands.size(); ++i)
		{
			if(!moderator || commands[i]->allowedForModerator())
			{
				if(tokens[0] == commands[i]->getCommandName())
				{
					if(!commands[i]->doesMatch(tokens.size()))
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
	}
	return false;
}


void YOGServerAdministrator::sendTextMessage(const std::string& message, std::shared_ptr<YOGServerPlayer> player)
{
	std::shared_ptr<YOGMessage> m(new YOGMessage(message, "admin", YOGAdministratorMessage));
	std::shared_ptr<NetSendYOGMessage> send(new NetSendYOGMessage(LOBBY_CHAT_CHANNEL, m));
	player->sendMessage(send);
}

