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

#include "boost/lexical_cast.hpp"
#include "FormatableString.h"
#include "NetMessage.h"
#include "StringTable.h"
#include "Toolkit.h"
#include "YOGClientBlockedList.h"
#include "YOGClientCommands.h"
#include "YOGClient.h"
#include "YOGClientPlayerListManager.h"

using namespace GAGCore;

std::string YOGClientBlockPlayerCommand::getHelpMessage()
{
	return Toolkit::getStringTable()->getString("[yog block command help]");
}



std::string YOGClientBlockPlayerCommand::getCommandName()
{
	return "/block";
}



bool YOGClientBlockPlayerCommand::doesMatch(const std::vector<std::string>& tokens)
{
	return tokens.size() == 2;
}



std::string YOGClientBlockPlayerCommand::execute(YOGClient* client, const std::vector<std::string>& tokens)
{
	if(client->getPlayerListManager()->doesPlayerExist(tokens[1]))
	{
		if(client->getBlockedList()->isPlayerBlocked(tokens[1]))
		{
			return FormatableString(Toolkit::getStringTable()->getString("[yog block command player %0 already blocked]")).arg(tokens[1]);
		}
		else
		{
			client->getBlockedList()->addBlockedPlayer(tokens[1]);
			client->getBlockedList()->save();
			return FormatableString(Toolkit::getStringTable()->getString("[yog block command player %0 blocked]")).arg(tokens[1]);
		}
	}
	return FormatableString(Toolkit::getStringTable()->getString("[yog block command player %0 not found]")).arg(tokens[1]);
}


