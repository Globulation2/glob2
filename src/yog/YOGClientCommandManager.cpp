// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "FormatableString.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGClientCommands.h"
#include "YOGClientCommandManager.h"

using namespace GAGCore;

YOGClientCommandManager::YOGClientCommandManager(YOGClient* client)
	: client(client)
{
	commands.push_back(new YOGClientBlockPlayerCommand);
}


	
YOGClientCommandManager::~YOGClientCommandManager()
{
	for(unsigned int i=0; i<commands.size(); ++i)
	{
		delete commands[i];
	}
}



std::string YOGClientCommandManager::executeClientCommand(const std::string& message)
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
		return "";
	}
	
	std::string text;
	if(tokens[0] == "/help")
	{
		text += Toolkit::getStringTable()->getString("[yog command header]");
		text += "\n";
		for(unsigned int i=0; i<commands.size(); ++i)
		{
			text += commands[i]->getHelpMessage() + '\n';
		}
		text += Toolkit::getStringTable()->getString("[yog help command help]");
	}
	else
	{
		for(unsigned int i=0; i<commands.size(); ++i)
		{
			if(tokens[0] == commands[i]->getCommandName())
			{
				if(!commands[i]->doesMatch(tokens))
				{
					text = commands[i]->getHelpMessage();
				}
				else
				{
					text = commands[i]->execute(client, tokens);
				}
				break;
			}
		}
	}
	if(tokens[0][0]=='/')
	{
		text += Toolkit::getStringTable()->getString("[yog command unknown]");
	}
	return text;
}

