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
	for(int i=0; i<commands.size(); ++i)
	{
		delete commands[i];
	}
}



std::string YOGClientCommandManager::executeClientCommand(const std::string& message)
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
		return "";
	}
	
	std::string text;
	if(tokens[0] == "/help")
	{
		text += Toolkit::getStringTable()->getString("[yog command header]");
		text += "\n";
		for(int i=0; i<commands.size(); ++i)
		{
			text += commands[i]->getHelpMessage() + '\n';
		}
		text += Toolkit::getStringTable()->getString("[yog help command help]");
	}
	else
	{
		for(int i=0; i<commands.size(); ++i)
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

