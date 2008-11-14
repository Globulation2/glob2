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

#include "YOGServerRouterAdministratorCommands.h"
#include "YOGServerRouter.h"

std::string YOGServerRouterAbortCommand::getHelpMessage()
{
	return "abort    Hard shuts down the router";
}



std::string YOGServerRouterAbortCommand::getCommandName()
{
	return "abort";
}



bool YOGServerRouterAbortCommand::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() == 1)
		return true;
	return false;
}



void YOGServerRouterAbortCommand::execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player)
{
	exit(0);
}



std::string YOGServerRouterShutdownCommand::getHelpMessage()
{
	return "shutdown    Disconnects from YOG and shuts off router once all clients are done";
}



std::string YOGServerRouterShutdownCommand::getCommandName()
{
	return "shutdown";
}



bool YOGServerRouterShutdownCommand::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() == 1)
		return true;
	return false;
}



void YOGServerRouterShutdownCommand::execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player)
{
	router->enterShutdownMode();
}




std::string YOGServerRouterStatusCommand::getHelpMessage()
{
	return "status    Prints a status report of the router";
}



std::string YOGServerRouterStatusCommand::getCommandName()
{
	return "status";
}



bool YOGServerRouterStatusCommand::doesMatch(const std::vector<std::string>& tokens)
{
	if(tokens.size() == 1)
		return true;
	return false;
}



void YOGServerRouterStatusCommand::execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player)
{
	admin->sendTextMessage(router->getStatusReport(), player);
}

