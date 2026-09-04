// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGServerRouterAdministratorCommand_h
#define YOGServerRouterAdministratorCommand_h

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
	
	///Returns true if the given set of tokens match whats required for this YOGServerRouterAdministratorCommand
	virtual bool doesMatch(const std::vector<std::string>& tokens)=0;
	
	///Executes the code for the administrator command
	virtual void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player)=0;
};

///This command hard shuts down the router
class YOGServerRouterAbortCommand : public YOGServerRouterAdministratorCommand
{
public:
	///Returns this YOGServerRouterAbortCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerRouterAbortCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerRouterAbortCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};


///This command causes a router to disconnect from the server and turn off once all clients disconnecty
class YOGServerRouterShutdownCommand : public YOGServerRouterAdministratorCommand
{
public:
	///Returns this YOGServerRouterShutdownCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerRouterShutdownCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerRouterShutdownCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};


///This command prints a status report of the YOG server
class YOGServerRouterStatusCommand : public YOGServerRouterAdministratorCommand
{
public:
	///Returns this YOGServerRouterStatusCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGServerRouterStatusCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGServerRouterStatusCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	void execute(YOGServerRouter* router, YOGServerRouterAdministrator* admin, const std::vector<std::string>& tokens, YOGServerRouterPlayer* player);
};

#endif
