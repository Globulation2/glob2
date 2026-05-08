// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include <string>
#include "FormatableString.h"
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


