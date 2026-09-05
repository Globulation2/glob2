// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <string>
#include <vector>
#include <memory>

class YOGServerRouterAdministrator;
class YOGServerRouter;
class YOGServerRouterPlayer;

///This defines a generic command
class YOGServerRouterAdministratorCommand
{
public:
	virtual ~YOGServerRouterAdministratorCommand() {}

	///Returns this YOGServerRouterAdministratorCommand help message
	virtual std::string getHelpMessage()=0;

	///Returns the command name for this YOGServerRouterAdministratorCommand
	virtual std::string getCommandName()=0;

	///Executes the code for the administrator command
	virtual void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player)=0;

	///Returns true if the token count is within this command's accepted range.
	bool doesMatch(std::size_t count) const
	{
		return int(count) >= minTokens && int(count) <= maxTokens;
	}

protected:
	explicit YOGServerRouterAdministratorCommand(int fixedTokens) : minTokens(fixedTokens), maxTokens(fixedTokens) {}
	YOGServerRouterAdministratorCommand(int min, int max) : minTokens(min), maxTokens(max) {}

private:
	int minTokens;
	int maxTokens;
};

///This command hard shuts down the router
class YOGServerRouterAbortCommand : public YOGServerRouterAdministratorCommand
{
public:
	YOGServerRouterAbortCommand() : YOGServerRouterAdministratorCommand(1) {}
	std::string getHelpMessage();
	std::string getCommandName();
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};


///This command causes a router to disconnect from the server and turn off once all clients disconnecty
class YOGServerRouterShutdownCommand : public YOGServerRouterAdministratorCommand
{
public:
	YOGServerRouterShutdownCommand() : YOGServerRouterAdministratorCommand(1) {}
	std::string getHelpMessage();
	std::string getCommandName();
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};


///This command prints a status report of the YOG server
class YOGServerRouterStatusCommand : public YOGServerRouterAdministratorCommand
{
public:
	YOGServerRouterStatusCommand() : YOGServerRouterAdministratorCommand(1) {}
	std::string getHelpMessage();
	std::string getCommandName();
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};

