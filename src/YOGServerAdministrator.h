// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef __YOGServerAdministrator_h
#define __YOGServerAdministrator_h

#include <string>
#include <memory>
#include <vector>

class YOGServer;
class YOGServerPlayer;
class YOGServerAdministratorCommand;

///This governs the system of administrative commands to the YOG server
class YOGServerAdministrator
{
public:
	///Constructs the administration engine
	YOGServerAdministrator(YOGServer* server);
	
	///Destroys the administration engine
	~YOGServerAdministrator();
	
	///Interprets whether the given message is an administrative command,
	///and if so, executes it. If it was, returns true, otherwise, returns
	///false
	bool executeAdministrativeCommand(const std::string& message, std::shared_ptr<YOGServerPlayer> player, bool moderator);
	
	///This sends a message to the player from the administrator engine
	void sendTextMessage(const std::string& message, std::shared_ptr<YOGServerPlayer> player);

private:

	YOGServer* server;
	
	std::vector<YOGServerAdministratorCommand*> commands;
};

#endif
