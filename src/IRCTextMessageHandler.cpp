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

#include "IRCTextMessageHandler.h"
#include "IRCThreadMessage.h"
#include <algorithm>
#include <functional>
#include <StringTable.h>
#include <Toolkit.h>
#include "YOGConsts.h"

using namespace GAGCore;
using std::static_pointer_cast;

IRCTextMessageHandler::IRCTextMessageHandler()
	: irc(incoming, incomingMutex)
{
	ircThread = std::thread(std::ref(irc));
	userListModified = false;
}


IRCTextMessageHandler::~IRCTextMessageHandler()
{
	//Tell the thread to exit and wait until it does
	std::shared_ptr<ITExitThread> message1(new ITExitThread);
	irc.sendMessage(message1);
	if (ircThread.joinable())
		ircThread.join();
}


void IRCTextMessageHandler::startIRC(const std::string& username)
{
	std::string nusername = username;
	while(nusername.find(" ") != std::string::npos)
	{
		nusername.replace(nusername.find(" "), 1, "_");
	}
	std::shared_ptr<ITConnect> message1(new ITConnect(IRC_SERVER, nusername, 6667));
	std::shared_ptr<ITJoinChannel> message2(new ITJoinChannel(IRC_CHAN));

	irc.sendMessage(message1);
	irc.sendMessage(message2);
}



void IRCTextMessageHandler::stopIRC()
{
	std::shared_ptr<ITDisconnect> message1(new ITDisconnect);
	irc.sendMessage(message1);
}



void IRCTextMessageHandler::update()
{
	std::lock_guard<std::recursive_mutex> lock(incomingMutex);
	while(!incoming.empty())
	{
		std::shared_ptr<IRCThreadMessage> message = incoming.front();
		incoming.pop();
		Uint8 type = message->getMessageType();
		switch(type)
		{
			case ITMRecieveMessage:
			{
				std::shared_ptr<ITRecieveMessage> info = static_pointer_cast<ITRecieveMessage>(message);
				sendToAllListeners(info->getMessage());
			}
			break;
			case ITMUserListModified:
			{
				std::shared_ptr<ITUserListModified> info = static_pointer_cast<ITUserListModified>(message);
				userListModified = true;
				users = info->getUsers();
			}
			break;
		}
	}	
}



void IRCTextMessageHandler::addTextMessageListener(IRCTextMessageListener* listener)
{
	listeners.push_back(listener);
}



void IRCTextMessageHandler::removeTextMessageListener(IRCTextMessageListener* listener)
{
	listeners.erase(std::find(listeners.begin(), listeners.end(), listener));
}



void IRCTextMessageHandler::sendCommand(const std::string& command)
{
	std::shared_ptr<ITSendMessage> message1(new ITSendMessage(command));
	irc.sendMessage(message1);
}



bool IRCTextMessageHandler::hasUserListBeenModified()
{
	if(userListModified)	
	{
		userListModified=false;
		return true;
	}
	return false;
}



std::vector<std::string>& IRCTextMessageHandler::getUsers()
{
	return users;
}



void IRCTextMessageHandler::sendToAllListeners(const std::string& message)
{
	for(unsigned i=0; i<listeners.size(); ++i)
	{
		listeners[i]->handleIRCTextMessage(message);
	}
}

