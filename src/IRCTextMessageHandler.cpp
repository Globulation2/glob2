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

#include <Toolkit.h>
#include <StringTable.h>
#include "YOGConsts.h"

using namespace GAGCore;

IRCTextMessageHandler::IRCTextMessageHandler()
{

}


IRCTextMessageHandler::~IRCTextMessageHandler()
{
	if(irc)
		irc->disconnect();
}


void IRCTextMessageHandler::startIRC(const std::string& username)
{
	irc.reset(new IRC);
	irc->connect(IRC_SERVER, 6667, username);
	irc->joinChannel(IRC_CHAN);
	irc->setChatChannel(IRC_CHAN);
}



void IRCTextMessageHandler::stopIRC()
{
	if(irc)
		irc->disconnect();
}



void IRCTextMessageHandler::update()
{
	if(irc)
	{
		irc->step();
	
		while (irc->isChatMessage())
		{
			std::string message;
			message+="<";
			message+=irc->getChatMessageSource();
			message+=">";
			message+=irc->getChatMessage();
			sendToAllListeners(message);
			irc->freeChatMessage();
		}
		
		while (irc->isInfoMessage())
		{
			std::string message;
			message += irc->getInfoMessageSource();
			
			switch (irc->getInfoMessageType())
			{
				case IRC::IRC_MSG_JOIN:
				message += " has joined irc channel ";
				break;
				
				case IRC::IRC_MSG_PART:
				message += " has left irc channel ";
				break;
				
				case IRC::IRC_MSG_QUIT:
				message += " has quitted irc, reason";
				break;
				
				case IRC::IRC_MSG_MODE:
				message += " has set mode of ";
				break;
				
				case IRC::IRC_MSG_NOTICE:
				if (irc->getInfoMessageSource()[0])
					message += " noticed ";
				else
					message += "Notice ";
				break;
				
				default:
				message += " has sent an unhandled IRC Info Message:";
				break;
			}
			
			if (irc->getInfoMessageDiffusion() != "")
			{
				message += irc->getInfoMessageDiffusion();
			}
			
			if (irc->getInfoMessageText() != "")
			{
				message += " : ";
				message += irc->getInfoMessageText();
			}
			sendToAllListeners(message);
			irc->freeInfoMessage();
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



boost::shared_ptr<IRC> IRCTextMessageHandler::getIRC()
{
	return irc;
}



void IRCTextMessageHandler::sendToAllListeners(const std::string& message)
{
	for(unsigned i=0; i<listeners.size(); ++i)
	{
		listeners[i]->handleIRCTextMessage(message);
	}
}

