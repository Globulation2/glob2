/*
  Standalone IRC client
  Copyright (C) 2001-2004 Stephane Magnenat
  for any question or comment contact me at nct@ysagoon.com

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

/*
	About IRC
	IRC is defined by RFC 2812
	Recent servers allows some inofficial protocol upgrades like longer nick length (16 on freenode instead of 9) for instance
*/

#include "IRC.h"
#include <iostream>
#include <GAG.h>
#include "GameGUIDialog.h"
#include "GlobalContainer.h"

// version related stuff
#ifdef HAVE_CONFIG_H
	#include <config.h>
#else
	#define PACKAGE_TARNAME "glob2"
	#define PACKAGE_VERSION "undef"
#endif

IRC::IRC()
{
	socket = NULL;
	socketSet = NULL;
}

IRC::~IRC()
{
	forceDisconnect();
}

bool IRC::connect(const char *serverName, int serverPort, const char *nick)
{
	disconnect();
	
	IPaddress ip;
	socketSet = SDLNet_AllocSocketSet(1);
	if (SDLNet_ResolveHost(&ip, (char *)serverName, serverPort)==-1)
	{
		fprintf(stderr, "YOG : ResolveHost: %s\n", SDLNet_GetError());
		return false;
	}

	socket = SDLNet_TCP_Open(&ip);
	if (!socket)
	{
		fprintf(stderr, "YOG : TCP_Open: %s\n", SDLNet_GetError());
		return false;
	}

	SDLNet_TCP_AddSocket(socketSet, socket);
        
	// Here we change the nick on yog for the IRC
	// changing from nick = "nick" to YOGnick = "YOGnick"
	memcpy(this->nick, "[YOG]", 5);
	strncpy(this->nick + 5, nick, IRC_NICK_SIZE - 5);
	this->nick[IRC_NICK_SIZE] = 0;

	char command[IRC_MESSAGE_SIZE];
	snprintf(command, IRC_MESSAGE_SIZE, "USER %9s undef undef %s-%s", this->nick, PACKAGE_TARNAME, PACKAGE_VERSION);
	sendString(command);
	snprintf(command, IRC_MESSAGE_SIZE, "NICK %9s", this->nick);
	sendString(command);

	return true;
}

bool IRC::disconnect(void)
{
	forceDisconnect();
	return true;
}

void IRC::forceDisconnect(void)
{
	if (socket)
	{
		SDLNet_TCP_Close(socket);
		socket = NULL;
	}
	if (socketSet)
	{
		SDLNet_FreeSocketSet(socketSet);
		socketSet = NULL;
	}
}

void IRC::interpreteIRCMessage(const char *message)
{
	char tempMessage[IRC_MESSAGE_SIZE];
	char *prefix;
	char *cmd;
	char *source;

	strncpy(tempMessage, message, IRC_MESSAGE_SIZE);

	// get informations about packet, homemade parser
	if (tempMessage[0]==':')
	{
		int i=1;
		while ((tempMessage[i]!=' ') && (tempMessage[i]!='!') && (tempMessage[i]!=0))
			i++;
		if (tempMessage[i]=='!')
		{
			tempMessage[i]=0;
			source=tempMessage+1;
			prefix=strtok(&tempMessage[i+1], " ");
			cmd=strtok(NULL, " ");
		}
		else if (tempMessage[i]==' ')
		{
			tempMessage[i]=0;
			source=tempMessage+1;
			cmd=strtok(&tempMessage[i+1], " ");
			prefix=NULL;
		}
		else
		{
			source=NULL;
			prefix=NULL;
			cmd=NULL;
			return;
		}
	}
	else
	{
		source=NULL;
		prefix=NULL;
		cmd=strtok(tempMessage, " ");
	}

	// this is a debug printf to reverse engineer IRC protocol
	//printf("IRC command is : [%s] Source is [%s]\n", cmd, source);
	if (strcasecmp(cmd, "PRIVMSG")==0)
	{
		char *diffusion=strtok(NULL, " :");
		char *message=strtok(NULL, "\0");
		
		// normal chat message
		ChatMessage msg;
		
		if (strstr(source, "[YOG]")==0)
		{
			if (message && (*(++message)))
			{
				strncpy(msg.source,  source, IRC_NICK_SIZE);
				msg.source[IRC_NICK_SIZE]=0;
		   
				strncpy(msg.diffusion,  diffusion, IRC_CHANNEL_SIZE);
				msg.diffusion[IRC_CHANNEL_SIZE]=0;

				strncpy(msg.message,  message, IRC_MESSAGE_SIZE);
				msg.message[IRC_MESSAGE_SIZE]=0;
			
				messages.push_back(msg);
			}
		}
	}
	else if (strcasecmp(cmd, "JOIN")==0)
	{
		char *diffusion=strtok(NULL, " :\0");
		InfoMessage msg(IRC_MSG_JOIN);
		
		strncpy(msg.source,  source, IRC_NICK_SIZE);
		msg.source[IRC_NICK_SIZE] = 0;
		strncpy(msg.diffusion,  diffusion, IRC_CHANNEL_SIZE);
		msg.diffusion[IRC_CHANNEL_SIZE] = 0;

		infoMessages.push_back(msg);
		
		usersOnChannels[std::string(diffusion)].insert(std::string(source));
		usersOnChannelsModified = true;
	}
	else if (strcasecmp(cmd, "PART")==0)
	{
		char *diffusion=strtok(NULL, " :");
		char *message = strtok(NULL, "\0");
		InfoMessage msg(IRC_MSG_PART);

		strncpy(msg.source,  source, IRC_NICK_SIZE);
		msg.source[IRC_NICK_SIZE] = 0;
		strncpy(msg.diffusion,  diffusion, IRC_CHANNEL_SIZE);
		msg.diffusion[IRC_CHANNEL_SIZE] = 0;
		if (message && (*(++message)))
		{
			strncpy(msg.message,  message, IRC_MESSAGE_SIZE);
			msg.message[IRC_MESSAGE_SIZE] = 0;
		}

		infoMessages.push_back(msg);
		
		// if we leave the chan, erase all list
		if (strcmp(source, nick) == 0)
			usersOnChannels[std::string(diffusion)].clear();
		else
			usersOnChannels[std::string(diffusion)].erase(std::string(source));
		usersOnChannelsModified = true;
	}
	else if (strcasecmp(cmd, "QUIT")==0)
	{
		char *message = strtok(NULL, "\0");
		InfoMessage msg(IRC_MSG_QUIT);

		strncpy(msg.source,  source, IRC_NICK_SIZE);
		msg.source[IRC_NICK_SIZE] = 0;
		if (message && (*(++message)))
		{
			strncpy(msg.message,  message, IRC_MESSAGE_SIZE);
			msg.message[IRC_MESSAGE_SIZE] = 0;
		}
		
		// erase this nick from all chans
		std::string user(source);
		for (std::map<std::string, std::set<std::string> >::iterator it = usersOnChannels.begin(); it!=usersOnChannels.end(); ++it)
		{
			if (it->second.find(user) != it->second.end())
			{
				it->second.erase(user);
				usersOnChannelsModified = true;
			}
		}

		infoMessages.push_back(msg);
	}
	else if (strcasecmp(cmd, "NOTICE")==0)
	{
		char *diffusion = strtok(NULL, " :");
		char *message = strtok(NULL, "\0");
	
		if ((diffusion != NULL) && (message != NULL))
		{
			InfoMessage msg(IRC_MSG_NOTICE);
			
			if (source)
			{
				strncpy(msg.source,  source, IRC_NICK_SIZE );
				msg.source[IRC_NICK_SIZE] = 0;
			
				strncpy(msg.diffusion,  diffusion, IRC_CHANNEL_SIZE);
				msg.diffusion[IRC_CHANNEL_SIZE] = 0;
				strncpy(msg.message,  message, IRC_MESSAGE_SIZE);
				msg.message[IRC_MESSAGE_SIZE] = 0;
				
				infoMessages.push_back(msg);
			}
		}
	}
	else if (strcasecmp(cmd, "353")==0)
	{
		char *diffusion = strtok(NULL, " =");
		char *chan = strtok(NULL, " :=");
		char *user;
		while ((user = strtok(NULL, " :")) != NULL)
		{
			usersOnChannels[std::string(chan)].insert(std::string(user));
			usersOnChannelsModified = true;
		}
	}

	else if (strcasecmp(cmd, "433")==0)
	{
		if (strlen(this->nick) < IRC_NICK_SIZE)
		{
			strcat(this->nick,"_");
			
			char command[IRC_MESSAGE_SIZE];
			snprintf(command, IRC_MESSAGE_SIZE, "NICK %9s", this->nick);
			sendString(command);
			joinChannel("#glob2");
		}
		
		else
		{
			const char *nicktaken = Toolkit::getStringTable()->getString("[nick taken]");
			const char *ok = Toolkit::getStringTable()->getString("[ok]");
			int res=(int)MessageBox(globalContainer->gfx, "standard", MB_ONEBUTTON, nicktaken, ok);
		
			if (res != 0 )
			{
				assert(false);
			}
		}
	}

}

void IRC::step(void)
{
	if (!socket)
		return;

	while (1)
	{
		int check = SDLNet_CheckSockets(socketSet, 0);
		if (check == 0)
		{
			break;
		}
		else if (check == 1)
		{
			char data[IRC_MESSAGE_SIZE];
			bool res=getString(data);
			if (res)
			{
				printf("YOG (IRC) has received [%s]\n", data);
				interpreteIRCMessage(data);
			}
			else
			{
				printf("YOG (IRC) has received an error\n");
				break;
			}
		}
		else
		{
			printf("YOG (IRC) has a select error\n");
			break;
		}
	}
}


bool IRC::isChatMessage(void)
{
	return messages.size()>0;
}

const char *IRC::getChatMessage(void)
{
	if (messages.size()>0)
		return messages[0].message;
	else
		return NULL;
}

const char *IRC::getChatMessageSource(void)
{
	if (messages.size()>0)
		return messages[0].source;
	else
		return NULL;
}

const char *IRC::getChatMessageDiffusion(void)
{
	if (messages.size()>0)
		return messages[0].diffusion;
	else
		return NULL;
}

void IRC::freeChatMessage(void)
{
	if (messages.size()>0)
		messages.erase(messages.begin());
}


bool IRC::isInfoMessage(void)
{
	return infoMessages.size()>0;
}

const IRC::InfoMessageType IRC::getInfoMessageType(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].type;
	else
		return IRC_MSG_NONE;
}

const char *IRC::getInfoMessageSource(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].source;
	else
		return NULL;
}

const char *IRC::getInfoMessageDiffusion(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].diffusion;
	else
		return NULL;
}

const char *IRC::getInfoMessageText(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].message;
	else
		return NULL;
}

void IRC::freeInfoMessage(void)
{
	if (infoMessages.size()>0)
		infoMessages.erase(infoMessages.begin());
}


void IRC::sendCommand(const char *message)
{
	char command[IRC_MESSAGE_SIZE];
	if (message[0]=='/')
	{
		char tempMessage[IRC_MESSAGE_SIZE];
		strncpy(tempMessage, message, IRC_MESSAGE_SIZE);
		char *cmd=strtok(tempMessage, " \t");
		char *arg1=strtok(NULL, " \t");
		char *arg2=strtok(NULL, "\0");
		printf ("c = [%s] a1 = [%s] a2 = [%s]\n", cmd, arg1, arg2);
		if ((strcasecmp(cmd, "/msg")==0) && arg1 && arg2)
		{
			snprintf(command, IRC_MESSAGE_SIZE, "PRIVMSG %s :%s", arg1, arg2);
			sendString(command);
		}
		else if ((strcasecmp(cmd, "/whois")==0) && arg1)
		{
			snprintf(command, IRC_MESSAGE_SIZE, "WHOIS %s", arg1);
			sendString(command);
		}
		else if ((strcasecmp(cmd, "/join")==0) && arg1)
		{
			joinChannel(arg1);
		}
		else if ((strcasecmp(cmd, "/part")==0) && arg1)
		{
			leaveChannel(arg1);
		}
		else if ((strcasecmp(cmd, "/leave")==0) && arg1)
		{
			leaveChannel(arg1);
		}
	}
	else
	{
		snprintf(command, IRC_MESSAGE_SIZE, "PRIVMSG %s :%s", chatChan, message);
		printf("YOG::sendString(%s).\n", command);
		sendString(command);
	}
}

void IRC::setChatChannel(const char *chan)
{
	strncpy(chatChan, chan, IRC_CHANNEL_SIZE);
	chatChan[IRC_CHANNEL_SIZE]=0;
}

void IRC::joinChannel(const char *channel)
{
	char command[IRC_MESSAGE_SIZE];
	
	if (channel==NULL)
		channel=chatChan;
	snprintf(command, IRC_MESSAGE_SIZE, "JOIN %s", channel);
	sendString(command);
}

void IRC::leaveChannel(const char *channel)
{
	char command[IRC_MESSAGE_SIZE];

	if (channel==NULL)
		channel=chatChan;
	snprintf(command, IRC_MESSAGE_SIZE, "PART %s", channel);
	sendString(command);
}

bool IRC::initChannelUserListing(const std::string &channel)
{
	std::map<std::string, std::set<std::string> >::const_iterator channelIt = usersOnChannels.find(channel);
	if (channelIt != usersOnChannels.end())
	{
		nextChannelUser = channelIt->second.begin();
		endChannelUser = channelIt->second.end();
		return true;
	}
	else
		return false;
}

bool IRC::isMoreChannelUser(void)
{
	return nextChannelUser != endChannelUser;
}

const std::string &IRC::getNextChannelUser(void)
{
	return *nextChannelUser++;
}

bool IRC::isChannelUserBeenModified(void)
{
	bool ret = usersOnChannelsModified;
	usersOnChannelsModified = false;
	return ret;
}

bool IRC::getString(char data[IRC_MESSAGE_SIZE])
{
	if (socket)
	{
		int i;
		int value;
		char c;

		i=0;
		while ( (  (value=SDLNet_TCP_Recv(socket, &c, 1)) >0) && (i<IRC_MESSAGE_SIZE-1))
		{
			if (c=='\r')
			{
				value=SDLNet_TCP_Recv(socket, &c, 1);
				if (value<=0)
					return false;
				else if (c=='\n')
					break;
				else
					return false;
			}
			else
			{
				data[i]=c;
			}
			i++;
		}
		data[i]=0;
		if (value<=0)
			return false;
		else
			return true;
	}
	else
	{
		return false;
	}
}

bool IRC::sendString(char *data)
{
	if (socket)
	{
		char ircMsg[IRC_MESSAGE_SIZE];
		snprintf(ircMsg, IRC_MESSAGE_SIZE-1, "%s", data);
		int len=strlen(ircMsg);
		ircMsg[len]='\r';
		ircMsg[len+1]='\n';
		len+=2;
		int result=SDLNet_TCP_Send(socket, ircMsg, len);
		return (result==len);
	}
	else
	{
		return false;
	}
}
