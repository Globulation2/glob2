// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "IRCThread.h"
#include "IRCThreadMessage.h"

using std::static_pointer_cast;

IRCThread::IRCThread(std::queue<std::shared_ptr<IRCThreadMessage> >& outgoing, std::recursive_mutex& outgoingMutex)
	: ThreadMessageQueues<IRCThreadMessage>(outgoing, outgoingMutex)
{
}


	
void IRCThread::operator()()
{
	while(true)
	{
		SDL_Delay(20);
		{
			//First parse incoming thread messages
			std::lock_guard<std::recursive_mutex> lock(incomingMutex);
			while(!incoming.empty())
			{
				std::shared_ptr<IRCThreadMessage> message = incoming.front();
				incoming.pop();
				Uint8 type = message->getMessageType();
				switch(type)
				{
					case ITMConnect:
					{
						std::shared_ptr<ITConnect> info = static_pointer_cast<ITConnect>(message);
						irc.connect(info->getServer(), info->getServerPort(), info->getNick());
					}
					break;
					case ITMDisconnect:
					{
						std::shared_ptr<ITDisconnect> info = static_pointer_cast<ITDisconnect>(message);
						irc.disconnect();
					}
					break;
					case ITMSendMessage:
					{
						std::shared_ptr<ITSendMessage> info = static_pointer_cast<ITSendMessage>(message);
						irc.sendCommand(info->getText());
					}
					break;
					case ITMJoinChannel:
					{
						std::shared_ptr<ITJoinChannel> info = static_pointer_cast<ITJoinChannel>(message);
						irc.joinChannel(info->getChannel());
						irc.setChatChannel(info->getChannel());
						channel = info->getChannel();
					}
					break;
					case ITMExitThread:
					{
						std::shared_ptr<ITExitThread> info = static_pointer_cast<ITExitThread>(message);
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
			std::shared_ptr<ITUserListModified> m(new ITUserListModified);

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
			std::shared_ptr<ITRecieveMessage> m(new ITRecieveMessage(message));
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
				message += " has quit irc, reason";
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
			std::shared_ptr<ITRecieveMessage> m(new ITRecieveMessage(message));
			sendToMainThread(m);
			irc.freeInfoMessage();
		}
	}
}



