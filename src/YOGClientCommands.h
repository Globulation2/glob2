// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGClientCommand_h
#define YOGClientCommand_h

#include <string>
#include <vector>
#include <memory>

class YOGClient;

///This defines a generic command
class YOGClientCommand
{
public:
	virtual ~YOGClientCommand() {}

	///Returns this YOGClientCommand help message
	virtual std::string getHelpMessage()=0;
	
	///Returns the command name for this YOGClientCommand
	virtual std::string getCommandName()=0;
	
	///Returns true if the given set of tokens match whats required for this YOGClientCommand
	virtual bool doesMatch(const std::vector<std::string>& tokens)=0;
	
	///Executes the code for the administrator command, returns the output from the command
	virtual std::string execute(YOGClient* client, const std::vector<std::string>& tokens)=0;
};

class YOGClientBlockPlayerCommand : public YOGClientCommand
{
public:
	///Returns this YOGClientCommand help message
	std::string getHelpMessage();
	
	///Returns the command name for this YOGClientCommand
	std::string getCommandName();
	
	///Returns true if the given set of tokens match whats required for this YOGClientCommand
	bool doesMatch(const std::vector<std::string>& tokens);
	
	///Executes the code for the administrator command
	std::string execute(YOGClient* client, const std::vector<std::string>& tokens);
};

#endif
