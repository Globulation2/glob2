// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

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

