/*
  Copyright (C) 2008 Bradley Arsenault

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

#include "IRCThread.h"
#include <iostream>

using namespace boost;

IRCThread::IRCThread(std::queue<boost::shared_ptr<IRCThreadMessage> >& outgoing, boost::recursive_mutex& outgoingMutex)
	: outgoing(outgoing), outgoingMutex(outgoingMutex)
{
	hasExited = false;
}


	
void IRCThread::operator()()
{
	while(true)
	{
		SDL_Delay(20);
		{
			//First parse incoming thread messages
			boost::recursive_mutex::scoped_lock lock(incomingMutex);
			while(!incoming.empty())
			{
				boost::shared_ptr<IRCThreadMessage> message = incoming.front();
				incoming.pop();
				Uint8 type = message->getMessageType();
				switch(type)
				{
					case ITMConnect:
					{
						boost::shared_ptr<ITConnect> info = static_pointer_cast<ITConnect>(message);
						irc.connect(info->getServer(), info->getServerPort(), info->getNick());
					}
					break;
					case ITMDisconnect:
					{
						boost::shared_ptr<ITDisconnect> info = static_pointer_cast<ITDisconnect>(message);
						irc.disconnect();
					}
					break;
					case ITMSendMessage:
					{
						boost::shared_ptr<ITSendMessage> info = static_pointer_cast<ITSendMessage>(message);
						irc.sendCommand(info->getText());
					}
					break;
					case ITMJoinChannel:
					{
						boost::shared_ptr<ITJoinChannel> info = static_pointer_cast<ITJoinChannel>(message);
						irc.joinChannel(info->getChannel());
						irc.setChatChannel(info->getChannel());
						channel = info->getChannel();
					}
					break;
					case ITMExitThread:
					{
						boost::shared_ptr<ITExitThread> info = static_pointer_cast<ITExitThread>(message);
						irc.disconnect();
						hasExited = true;
						return;
					}
					break;
				}
			}
		}


		irc.step();
		if(irc.isChannelUserBeenModified())
		{
			boost::shared_ptr<ITUserListModified> m(new ITUserListModified);

			if (irc.initChannelUserListing(channel))
			{
				while (irc.isMoreChannelUser())
				{
					const std::string &user = irc.getNextChannelUser();
					m->addUser(user);
				}
			}
			sendToMainThread(m);
		}

		while (irc.isChatMessage())
		{
			std::string message;
			message+="<";
			message+=irc.getChatMessageSource();
			message+=">";
			message+=irc.getChatMessage();
			boost::shared_ptr<ITRecieveMessage> m(new ITRecieveMessage(message));
			sendToMainThread(m);
			irc.freeChatMessage();
		}

		while (irc.isInfoMessage())
		{
			std::string message;
			message += irc.getInfoMessageSource();
			
			switch (irc.getInfoMessageType())
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
				if (irc.getInfoMessageSource()[0])
					message += " noticed ";
				else
					message += "Notice ";
				break;
				
				default:
				message += " has sent an unhandled IRC Info Message:";
				break;
			}
			
			if (irc.getInfoMessageDiffusion() != "")
			{
				message += irc.getInfoMessageDiffusion();
			}
			
			if (irc.getInfoMessageText() != "")
			{
				message += " : ";
				message += irc.getInfoMessageText();
			}
			boost::shared_ptr<ITRecieveMessage> m(new ITRecieveMessage(message));
			sendToMainThread(m);
			irc.freeInfoMessage();
		}
	}
}



void IRCThread::sendMessage(boost::shared_ptr<IRCThreadMessage> message)
{
	boost::recursive_mutex::scoped_lock lock(incomingMutex);
	incoming.push(message);
}



bool IRCThread::hasThreadExited()
{
	return hasExited;
}



void IRCThread::sendToMainThread(boost::shared_ptr<IRCThreadMessage> message)
{
	boost::recursive_mutex::scoped_lock lock(outgoingMutex);
	outgoing.push(message);
}


