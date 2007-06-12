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

#include "NetTextMessageHandler.h"

#include <Toolkit.h>
#include <StringTable.h>

using namespace GAGCore;

NetTextMessageHandler::NetTextMessageHandler(shared_ptr<YOGClient> client)
	: client(client)
{

}


NetTextMessageHandler::~NetTextMessageHandler()
{
	irc->disconnect();
}


void NetTextMessageHandler::startIRC()
{
	irc.reset(new IRC);
	irc->connect(IRC_SERVER, 6667, client->getUsername());
	irc->joinChannel(IRC_CHAN);
	irc->setChatChannel(IRC_CHAN);
}



void NetTextMessageHandler::stopIRC()
{
	irc->disconnect();
}



void NetTextMessageHandler::update()
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
			sendToAllListeners(message, IRCTextMessage);
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
			sendToAllListeners(message, IRCTextMessage);
			irc->freeInfoMessage();
		}
	
	}

	boost::shared_ptr<YOGMessage> message = client->getMessage();
	while (message)
	{
		std::string smessage;
		switch(message->getMessageType())
		{
			case YOGNormalMessage:
				smessage+="<";
				smessage+=message->getSender();
				smessage+="> ";
				smessage+=message->getMessage();
			break;
			case YOGPrivateMessage:
				smessage+="<";
				smessage+=Toolkit::getStringTable()->getString("[from:]");
				smessage+=message->getSender();
				smessage+="> ";
				smessage+=message->getMessage();
			break;
			case YOGAdministratorMessage:
				smessage+="[";
				smessage+=message->getSender();
				smessage+="] ";
				smessage+=message->getMessage();
			break;
			case YOGGameMessage:
				smessage+="<";
				smessage+=message->getSender();
				smessage+="> ";
				smessage+=message->getMessage();
			break;
			default:
				assert(false);
			break;
		}
		if(message->getMessageType() == YOGGameMessage)
			sendToAllListeners(smessage, PreGameYOGTextMessage);
		else
			sendToAllListeners(smessage, YOGTextMessage);
		message = client->getMessage();
	}

}



void NetTextMessageHandler::addTextMessageListener(NetTextMessageListener* listener)
{
	listeners.push_back(listener);
}



void NetTextMessageHandler::removeTextMessageListener(NetTextMessageListener* listener)
{
	listeners.erase(std::find(listeners.begin(), listeners.end(), listener));
}



boost::shared_ptr<IRC> NetTextMessageHandler::getIRC()
{
	return irc;
}



void NetTextMessageHandler::sendToAllListeners(const std::string& message, NetTextMessageType type)
{
	for(unsigned i=0; i<listeners.size(); ++i)
	{
		listeners[i]->handleTextMessage(message, type);
	}
}
