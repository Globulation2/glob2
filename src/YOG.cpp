/*
    Copyright (C) 2001, 2002 Stephane Magnenat
    for any question or comment contact us at nct@ysagoon.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "YOG.h"

YOG::YOG()
{
	socket=NULL;
	socketSet=NULL;
}

YOG::~YOG()
{

}

bool YOG::connect(const char *serverName, int serverPort, const char *nick)
{
	IPaddress ip;
	socketSet=SDLNet_AllocSocketSet(1);
	if(SDLNet_ResolveHost(&ip, (char *)serverName, serverPort)==-1)
	{
		fprintf(stderr, "YOG : ResolveHost: %s\n", SDLNet_GetError());
		return false;
	}

	socket=SDLNet_TCP_Open(&ip);
	if(!socket)
	{
		fprintf(stderr, "YOG : TCP_Open: %s\n", SDLNet_GetError());
		return false;
	}

	SDLNet_TCP_AddSocket(socketSet, socket);

	char command[IRC_MESSAGE_SIZE];
	snprintf(command, IRC_MESSAGE_SIZE, "USER %s undef undef Glob2_User", nick);
	sendString(command);
	snprintf(command, IRC_MESSAGE_SIZE, "NICK %s", nick);
	sendString(command);
	joinChannel();
	return true;
}

void YOG::forceDisconnect(void)
{
	if (socket)
	{
		SDLNet_TCP_Close(socket);
		socket=NULL;
	}
	if (socketSet)
	{
		SDLNet_FreeSocketSet(socketSet);
		socketSet=NULL;
	}
}

void YOG::interpreteIRCMessage(const char *message)
{
	char tempMessage[IRC_MESSAGE_SIZE];
	char *prefix;
	char *cmd;
	char *source;

	strncpy(tempMessage, message, IRC_MESSAGE_SIZE);

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
		}
	}
	else
	{
		source=NULL;
		prefix=NULL;
		cmd=strtok(tempMessage, " ");
	}

	printf("IRC command is : [%s] Source is [%s]\n", cmd, source);

	if (strcasecmp(cmd, "PRIVMSG")==0)
	{
		char *diffusion=strtok(NULL, " :");
		char *message=strtok(NULL, ":\0");
		ChatMessage msg;

		strncpy(msg.source,  source, IRC_NICK_SIZE);
		strncpy(msg.diffusion,  diffusion, IRC_CHANNEL_SIZE);
		strncpy(msg.message,  message, IRC_MESSAGE_SIZE);

		messages.push_back(msg);
	}
}

void YOG::step(void)
{
	while (1)
	{
		int check=SDLNet_CheckSockets(socketSet, 0);
		if (check==1)
		{
			char data[IRC_MESSAGE_SIZE];
			bool res=getString(data);
			if (res)
			{
				printf("YOG (IRC) has received [%s]\n", data);
				interpreteIRCMessage(data);
			}
			else
				printf("YOG (IRC) has received an error\n");

		}
		if (check==-1)
			printf("YOG (IRC) has a select error\n");
		if (check==0)
			break;
	}
}

bool YOG::isChatMessage(void)
{
	return messages.size()>0;
}

const char *YOG::getChatMessage(void)
{
	if (messages.size()>0)
		return messages[0].message;
	else
		return NULL;
}

const char *YOG::getChatMessageSource(void)
{
	if (messages.size()>0)
		return messages[0].source;
	else
		return NULL;
}

const char *YOG::getMessageDiffusion(void)
{
	if (messages.size()>0)
		return messages[0].diffusion;
	else
		return NULL;
}

void YOG::freeChatMessage(void)
{
	if (messages.size()>0)
		messages.erase(messages.begin());
}

void YOG::sendCommand(const char *message)
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
	}
	else
	{
		snprintf(command, IRC_MESSAGE_SIZE, "PRIVMSG %s :%s", DEFAULT_CHAT_CHAN, message);
		sendString(command);
	}
}

void YOG::joinChannel(const char *channel)
{
	char command[IRC_MESSAGE_SIZE];
	snprintf(command, IRC_MESSAGE_SIZE, "JOIN %s", channel);
	sendString(command);
}

void YOG::quitChannel(const char *channel)
{
	char command[IRC_MESSAGE_SIZE];
	snprintf(command, IRC_MESSAGE_SIZE, "PART %s", channel);
	sendString(command);
}

bool YOG::getString(char data[IRC_MESSAGE_SIZE])
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

bool YOG::sendString(char *data)
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
