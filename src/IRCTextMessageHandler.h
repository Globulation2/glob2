/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef __IRCTextMessageHandler_H
#define __IRCTextMessageHandler_H

#include "IRC.h"
#include "boost/shared_ptr.hpp"


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

	///Returns the IRC server to send commands to it
	boost::shared_ptr<IRC> getIRC();
	
	///Adds a new internal message
	void addInternalMessage(const std::string& message);

private:
	void sendToAllListeners(const std::string& message);

	boost::shared_ptr<IRC> irc;
	std::vector<IRCTextMessageListener* > listeners;
};


#endif
