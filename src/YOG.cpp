/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

/*
	TODO to finish :
	
	DONE - implement a way to activate new game claiming
	DONE - call yog.step from main game, multiplayerhost,
	and yogscreen
	DONE - do a lookup from nick to IP using IRC whois
	command (need to parse the command).
	TODO - clean and test all stuff

*/

#include "YOG.h"
#define DEFAULT_FW_CHAN "#yog-fw"

YOG::YOG()
{
	socket=NULL;
	socketSet=NULL;
	isSharedGame=false;
	sharedGame[0]=0;
}

YOG::~YOG()
{
	forceDisconnect();
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
	snprintf(command, IRC_MESSAGE_SIZE, "USER %9s undef undef Glob2_User", nick);
	sendString(command);
	snprintf(command, IRC_MESSAGE_SIZE, "NICK %9s", nick);
	sendString(command);

	strncpy(this->nick, nick, IRC_NICK_SIZE);
	this->nick[IRC_NICK_SIZE]=0;

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
		if ((diffusion) && (strncmp(diffusion, DEFAULT_FW_CHAN, IRC_CHANNEL_SIZE)==0))
		{
			char *nick=strtok(NULL, " :\0");
			char *port=strtok(NULL, " :\0");

			if ((nick) && (port) && (prefix) && (strncmp(this->nick, nick, IRC_NICK_SIZE)==0))
			{
				FirewallActivation fwAct;

				char *startIp;
				if ((startIp=strrchr(prefix, '@'))!=NULL)
				{
					startIp++;
					strncpy(fwAct.hostname, startIp, FW_ACT_HOSTNAME_SIZE+prefix-startIp);
				}
				else
				{
					strncpy(fwAct.hostname, prefix, FW_ACT_HOSTNAME_SIZE);
				}
				fwAct.hostname[FW_ACT_HOSTNAME_SIZE]=0;

				fwAct.port=atoi(port);

				firewallActivations.push_back(fwAct);
				//printf("YOG : UDP : %s request UDP packet on port %d\n", fwAct.hostname, fwAct.port);
			}
		}
		else if ((diffusion) && (strncmp(diffusion, DEFAULT_GAME_CHAN, IRC_CHANNEL_SIZE)==0))
		{
			// filter data to game list
			char *identifier=strtok(NULL, " :\0");
			char *version=strtok(NULL, " :\0");
			char *comment=strtok(NULL, ":\0");

			if ((source!=NULL) && (identifier!=NULL) && (version!=NULL) && (comment!=NULL))
			{
				GameInfo msg;

				strncpy(msg.source,  source, IRC_NICK_SIZE);
				msg.source[IRC_NICK_SIZE]=0;

				strncpy(msg.identifier, identifier, GAMEINFO_ID_SIZE);
				msg.identifier[GAMEINFO_ID_SIZE]=0;

				strncpy(msg.version, version, GAMEINFO_VERSION_SIZE);
				msg.version[GAMEINFO_VERSION_SIZE]=0;

				strncpy(msg.comment, comment, GAMEINFO_COMMENT_SIZE);
				msg.comment[GAMEINFO_COMMENT_SIZE]=0;

				if (prefix)
				{
					char *startIp;
					if ((startIp=strrchr(prefix, '@'))!=NULL)
					{
						startIp++;
						strncpy(msg.hostname, startIp, GAMEINFO_HOSTNAME_SIZE+prefix-startIp);
					}
					else
					{
						strncpy(msg.hostname, prefix, GAMEINFO_HOSTNAME_SIZE);
					}
					msg.hostname[GAMEINFO_HOSTNAME_SIZE]=0;
				}
				else
				{
					msg.hostname[0]=0;
				}

				msg.updatedTick=SDL_GetTicks();

				// search game with same source, and replace it
				std::vector<GameInfo>::iterator alreadyExists;
				bool isNew=true;

				for (alreadyExists=gameInfos.begin(); alreadyExists!=gameInfos.end(); ++alreadyExists)
				{
					if (strncmp((*alreadyExists).source,  msg.source, IRC_NICK_SIZE)==0)
					{
						(*alreadyExists)=msg;
						isNew=false;
						break;
					}
				}

				if (isNew)
					gameInfos.push_back(msg);
			}
		}
		else
		{
			// normal chat message
			ChatMessage msg;

			char *message=strtok(NULL, "\0");
			message=strchr(message, ':');

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
		InfoMessage msg;

		msg.type=IRC_MSG_JOIN;

		strncpy(msg.source,  source, IRC_NICK_SIZE);
		msg.source[IRC_NICK_SIZE]=0;

		if (diffusion)
		{
			strncpy(msg.diffusion,  diffusion, IRC_CHANNEL_SIZE);
			msg.diffusion[IRC_CHANNEL_SIZE]=0;
		}
		else
			msg.diffusion[0]=0;

		infoMessages.push_back(msg);
	}
	else if (strcasecmp(cmd, "QUIT")==0)
	{
		InfoMessage msg;

		msg.type=IRC_MSG_QUIT;

		strncpy(msg.source,  source, IRC_NICK_SIZE);
		msg.source[IRC_NICK_SIZE]=0;

		msg.diffusion[0]=0;

		infoMessages.push_back(msg);
	}
}

void YOG::step(void)
{
	if (!socket)
		return;

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
			{
				printf("YOG (IRC) has received an error\n");
				break;
			}
		}
		if (check==-1)
			printf("YOG (IRC) has a select error\n");
		if (check==0)
			break;
	}

	// delete timeout game
	std::vector<GameInfo>::iterator gameToDeleteIt=gameInfos.begin();
	while (gameToDeleteIt!=gameInfos.end())
	{
		if (((*gameToDeleteIt).updatedTick+RESEND_GAME_TIMEOUT)<SDL_GetTicks())
			gameToDeleteIt=gameInfos.erase(gameToDeleteIt);
		else
			++gameToDeleteIt;
	}

	// resend game if timout between two send has elapsed
	if (isSharedGame)
	{
		if (sharedGameLastUpdated+RESEND_GAME_INTERVAL<SDL_GetTicks())
		{
			sendString(sharedGame);
			sharedGameLastUpdated=SDL_GetTicks();
		}
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


bool YOG::isInfoMessage(void)
{
	return infoMessages.size()>0;
}

const YOG::InfoMessageType YOG::getInfoMessageType(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].type;
	else
		return IRC_MSG_NONE;
}

const char *YOG::getInfoMessageSource(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].source;
	else
		return NULL;
}

const char *YOG::getInfoMessageDiffusion(void)
{
	if (infoMessages.size()>0)
		return infoMessages[0].diffusion;
	else
		return NULL;
}

void YOG::freeInfoMessage(void)
{
	if (infoMessages.size()>0)
		infoMessages.erase(infoMessages.begin());
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
		else if ((strcasecmp(cmd, "/join")==0) && arg1)
		{
			joinChannel(arg1);
		}
		else if ((strcasecmp(cmd, "/part")==0) && arg1)
		{
			quitChannel(arg1);
		}
	}
	else
	{
		snprintf(command, IRC_MESSAGE_SIZE, "PRIVMSG %s :%s", chatChan, message);
		sendString(command);
	}
}

void YOG::setChatChannel(const char *chan)
{
	strncpy(chatChan, chan, IRC_CHANNEL_SIZE);
	chatChan[IRC_CHANNEL_SIZE]=0;
}

void YOG::joinChannel(const char *channel)
{
	char command[IRC_MESSAGE_SIZE];
	
	if (channel==NULL)
		channel=chatChan;
	snprintf(command, IRC_MESSAGE_SIZE, "JOIN %s", channel);
	sendString(command);
}

void YOG::quitChannel(const char *channel)
{
	char command[IRC_MESSAGE_SIZE];

	if (channel==NULL)
		channel=chatChan;
	snprintf(command, IRC_MESSAGE_SIZE, "PART %s", channel);
	sendString(command);
}


void YOG::unshareGame(void)
{
	isSharedGame=false;
	quitChannel(DEFAULT_FW_CHAN);
}

void YOG::shareGame(const char *id, const char *version, const char *comment)
{
	isSharedGame=true;
	sharedGameLastUpdated=SDL_GetTicks();
	snprintf(sharedGame, sizeof(sharedGame), "PRIVMSG %s :%s %s %s", DEFAULT_GAME_CHAN, id, version, comment);
	sendString(sharedGame);
	joinChannel(DEFAULT_FW_CHAN);
}


bool YOG::resetGameLister(void)
{
	gameInfoIt=gameInfos.begin();
	return (gameInfos.size()!=0);
}

const char *YOG::getGameSource(void)
{
	return (*gameInfoIt).source;
}

const char *YOG::getGameIdentifier(void)
{
	return (*gameInfoIt).identifier;
}

const char *YOG::getGameVersion(void)
{
	return (*gameInfoIt).version;
}

const char *YOG::getGameComment(void)
{
	return (*gameInfoIt).comment;
}

const char *YOG::getGameHostname(void)
{
	return (*gameInfoIt).hostname;
}

const YOG::GameInfo *YOG::getGameInfo(void)
{
	return &(*gameInfoIt);
}

bool YOG::getNextGame(void)
{
	gameInfoIt++;
	if (gameInfoIt!=gameInfos.end())
	{
		return true;
	}
	else
	{
		return false;
	}
}


bool YOG::isFirewallActivation()
{
	return (firewallActivations.size()>0);
}

Uint16 YOG::getFirewallActivationPort(void)
{
	if (firewallActivations.size()>0)
		return firewallActivations[0].port;
	else
		return 0;
}

char *YOG::getFirewallActivationHostname(void)
{
	if (firewallActivations.size()>0)
		return firewallActivations[0].hostname;
	else
		return NULL;
}

bool YOG::getNextFirewallActivation(void)
{
	firewallActivations.erase(firewallActivations.begin());
	if (firewallActivations.size()>0)
		return true;
	else
		return false;
}

void YOG::sendFirewallActivation(const char *nick, Uint16 port)
{
	joinChannel(DEFAULT_FW_CHAN);

	char activationMssage[IRC_MESSAGE_SIZE];
	snprintf(activationMssage, sizeof(activationMssage), "PRIVMSG %s :%s %d", DEFAULT_FW_CHAN, nick, port);
	sendString(activationMssage);

	quitChannel(DEFAULT_FW_CHAN);
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
