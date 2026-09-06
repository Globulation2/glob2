// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include <string>
#include <memory>
#include <vector>
#include "RouterAdminMessages.h"
#include "YOGServerRouterAdministrator.h"
#include "YOGServerRouterAdministratorCommands.h"
#include "YOGServerRouterPlayer.h"


YOGServerRouterAdministrator::YOGServerRouterAdministrator(YOGServerRouter* router)
	: router(router)
{
	commands.push_back(std::make_unique<YOGServerRouterShutdownCommand>());
	commands.push_back(std::make_unique<YOGServerRouterAbortCommand>());
	commands.push_back(std::make_unique<YOGServerRouterStatusCommand>());
}



YOGServerRouterAdministrator::~YOGServerRouterAdministrator() = default;



bool YOGServerRouterAdministrator::executeAdministrativeCommand(const std::string& message, YOGServerRouterPlayer* player)
{
	allText.clear();
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
		sendTextMessage("Use help to get a list of commands", player);
		flushTexts(player);
		return false;
	}


	if(tokens[0] == "help")
	{
		sendTextMessage("The current list of YOG Router Administrative Commands are: ", player);
		for(unsigned int i=0; i<commands.size(); ++i)
		{
			sendTextMessage(commands[i]->getHelpMessage(), player);
		}
		sendTextMessage("help    Shows this help message", player);
		flushTexts(player);
		return true;
	}
	else
	{
		for(unsigned int i=0; i<commands.size(); ++i)
		{
			if(tokens[0] == commands[i]->getCommandName())
			{
				if(!commands[i]->doesMatch(tokens.size()))
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
	std::shared_ptr<NetRouterAdministratorCommandResponse> text(new NetRouterAdministratorCommandResponse(allText));
	admin->sendNetMessage(text);
	allText.clear();
}

