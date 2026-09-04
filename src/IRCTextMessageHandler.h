// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __IRCTextMessageHandler_H
#define __IRCTextMessageHandler_H

#include "IRCThread.h"
#include <thread>
#include <memory>


///This class represents an object that can listen for text messages from IRC
class IRCTextMessageListener
{
public:
	virtual ~IRCTextMessageListener() {}
	///This function is meant to handle a text message
	virtual void handleIRCTextMessage(const std::string& message)=0;
};


///This system puts together and formats messages the two sources, YOG and IRC, for the lobby
class IRCTextMessageHandler
{
public:
	///Starts listening to the messages coming from IRC
	IRCTextMessageHandler();
	
	~IRCTextMessageHandler();

	///Connects to the IRC server and begins taking messages from it
	void startIRC(const std::string& username);
	
	///Disconnect from IRC
	void stopIRC();

	///Updates the handler
	void update();

	///Adds a listener to listen for text messages
	void addTextMessageListener(IRCTextMessageListener* listener);

	///Removes a listener
	void removeTextMessageListener(IRCTextMessageListener* listener);

	///Sends a command to the IRC engine
	void sendCommand(const std::string& command);

	///Tells whether the user list has been modified
	bool hasUserListBeenModified();

	///Returns the user list
	std::vector<std::string>& getUsers();
private:
	void sendToAllListeners(const std::string& message);

	IRCThread irc;
	std::thread ircThread;
	std::vector<IRCTextMessageListener* > listeners;

	std::queue<std::shared_ptr<IRCThreadMessage> > incoming;
	std::recursive_mutex incomingMutex;
	std::vector<std::string> users;


	bool userListModified;
		
};


#endif
