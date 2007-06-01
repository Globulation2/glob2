/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __NetTextMessageHandler_H
#define __NetTextMessageHandler_H

#include "IRC.h"
#include "YOGClient.h"

///This system puts together and formats messages the two sources, YOG and IRC
class NetTextMessageHandler
{
public:
	///Starts listening to the messages coming from client
	NetTextMessageHandler(shared_ptr<YOGClient> client);
	
	~NetTextMessageHandler();

	///Connects to the IRC server and begins taking messages from it
	void startIRC();
	
	///Disconnect from IRC
	void stopIRC();

	///Updates the handler
	void update();
	
	///Returns the next message in the queue, and removes it.
	///An empty text means there are no more
	std::string getNextMessage();
	
	enum TextMessageType
	{
		IRCTextMessage,
		YOGTextMessage,
		NoTextMessage,
	};
	
	///Returns the type of the message, either from IRC. Should be used in conjunction
	///with getNextMessage, as it has the same queue idea
	TextMessageType getNextMessageType();
	
	///Returns the IRC server to send commands to it
	boost::shared_ptr<IRC> getIRC();

private:
	boost::shared_ptr<YOGClient> client;
	boost::shared_ptr<IRC> irc;
	std::queue<std::string> messages;
	std::queue<TextMessageType> messageTypes;
};

#endif
