// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <string>
#include <memory>
#include <vector>

class YOGServerRouter;
class YOGServerRouterPlayer;
class YOGServerRouterAdministratorCommand;

///This governs the system of administrative commands to the YOG server
class YOGServerRouterAdministrator
{
public:
	///Constructs the administration engine
	YOGServerRouterAdministrator(YOGServerRouter* router);
	
	///Destroys the administration engine
	~YOGServerRouterAdministrator();
	
	///Interprets whether the given message is an administrative command,
	///and if so, executes it. If it was, returns true, otherwise, returns
	///false
	bool executeAdministrativeCommand(const std::string& message, YOGServerRouterPlayer* player);
	
	///This sends a message to the player from the administrator engine
	void sendTextMessage(const std::string& message, YOGServerRouterPlayer* admin);

	///Flushes the text
	void flushTexts(YOGServerRouterPlayer* admin);

private:
	YOGServerRouter* router;
	std::vector<std::unique_ptr<YOGServerRouterAdministratorCommand>> commands;
	std::string allText;
};

