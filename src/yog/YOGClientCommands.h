// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

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

	///Executes the code for the administrator command, returns the output from the command
	virtual std::string execute(YOGClient* client, const std::vector<std::string>& tokens)=0;

	///Returns true if the token count is within this command's accepted range.
	bool doesMatch(std::size_t count) const
	{
		return int(count) >= minTokens && int(count) <= maxTokens;
	}

protected:
	explicit YOGClientCommand(int fixedTokens) : minTokens(fixedTokens), maxTokens(fixedTokens) {}
	YOGClientCommand(int min, int max) : minTokens(min), maxTokens(max) {}

private:
	int minTokens;
	int maxTokens;
};

class YOGClientBlockPlayerCommand : public YOGClientCommand
{
public:
	YOGClientBlockPlayerCommand() : YOGClientCommand(2) {}
	std::string getHelpMessage();
	std::string getCommandName();
	std::string execute(YOGClient* client, const std::vector<std::string>& tokens);
};

