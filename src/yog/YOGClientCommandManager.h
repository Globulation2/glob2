// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <string>
#include <memory>
#include <vector>

class YOGClient;
class YOGClientCommand;

///This manages client commands, like /block
class YOGClientCommandManager
{
public:
	YOGClientCommandManager(YOGClient* client);
	
	///Destroys the administration engine
	~YOGClientCommandManager();

	///Interprets whether the given message is a client command, and if so
	///executes it. If it wasn't a command, the string this returns will be
	///empty
	std::string executeClientCommand(const std::string& message);
	
private:
	YOGClient* client;
	std::vector<std::unique_ptr<YOGClientCommand>> commands;
};


