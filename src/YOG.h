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

#ifndef __YOG_H
#define __YOG_H

#include "Header.h"
#include <deque>
#include <vector>

#define DEFAULT_CHAT_CHAN "#debian"
#define DEFAULT_GAME_CHAN "#yog-games"

//! This class is a YOG client
class YOG
{
public:
	enum { IRC_CHANNEL_SIZE = 200, IRC_MESSAGE_SIZE=512, IRC_NICK_SIZE=9 };
	enum { YOG_GAMEINFO_ID_SIZE = 32, YOG_GAMEINFO_VERSION_SIZE=8, YOG_GAMEINFO_COMMENT_SIZE=60 };
	enum InfoMessageType
	{
		IRC_MSG_NONE=0,
		IRC_MSG_JOIN=1,
		IRC_MSG_QUIT=2,
		IRC_MSG_MODE=3
	};

	struct ChatMessage
	{
		char message[IRC_MESSAGE_SIZE+1];
		char source[IRC_NICK_SIZE+1];
		char diffusion[IRC_CHANNEL_SIZE+1];
	};

	struct InfoMessage
	{
		InfoMessageType type;
		char source[IRC_NICK_SIZE+1];
		char diffusion[IRC_CHANNEL_SIZE+1];
	};

	struct GameInfo
	{
		char source[IRC_NICK_SIZE+1];
		char identifier[YOG_GAMEINFO_ID_SIZE+1];
		char version[YOG_GAMEINFO_VERSION_SIZE+1];
		char comment[YOG_GAMEINFO_COMMENT_SIZE+1];
		Uint32 updatedTick;
	};

protected:
	//! the socket that we use to connect to IRC
	TCPsocket socket;
	//! the set for our socket; used to know if new data are available
	SDLNet_SocketSet socketSet;

	//! pending messages
	std::deque<ChatMessage> messages;

	//! pending info message
	std::deque<InfoMessage> infoMessages;
	
	//! games infos
	std::vector<GameInfo> gameInfos;

	//! iterator for get function from user
	std::vector<GameInfo>::iterator gameInfoIt;

protected:
	//! Interprete a message from IRC; do parsing etc
	void interpreteIRCMessage(const char *message);

public:
	//! YOG Constructor
	YOG();
	//! YOG Destructor
	virtual ~YOG();

	// CONNECTION
	//! Connect to YOG server (IRC network), return true on sucess
	bool connect(const char *serverName, int serverPort, const char *nick);
	//! Try to disconnect from server in a cleany way
	bool disconnect(void);
	//! Force disconnect (kill socket)
	void forceDisconnect(void);

	// RUN
	//! Do a YOG step
	void step(void);

	// CHAT
	//! Return true if there is pending message
	bool isChatMessage(void);
	//! Get message
	const char *getChatMessage(void);
	//! Get the user from where the message is
	const char *getChatMessageSource(void);
	//! Get where the message has been spawned
	const char *getMessageDiffusion(void);
	//! Free last message
	void freeChatMessage(void);
	//! Send a message (or a command)
	void sendCommand(const char *message);

	// INFO
	//! Return true if there is pending info message
	bool isInfoMessage(void);
	//! Get Info info message type
	const InfoMessageType getInfoMessageType(void);
	//! Get the associated nick with info message
	const char *getInfoMessageSource(void);
	//! Get where the info message has been spawned
	const char *getInfoMessageDiffusion(void);
	//! Free last info message
	void freeInfoMessage(void);

	//! Join a given channel
	void joinChannel(const char *channel=DEFAULT_CHAT_CHAN);
	//! Quit a given channel
	void quitChannel(const char *channel=DEFAULT_CHAT_CHAN);

	// GAME creation
	//! Create a new game and start the login room
	void createGame(void);
	//! Start the game
	void startGame(void);
	//! Set the game parameters
	void setGameParameters(const char *id, const char *version, const char *comment);

	// GAME joining
	/*
		Typical use :
		if (yog.resetGameLister())
		{
			do
			{
				...=yog.getGameSource();
				...=yog.getGameIndetifier();
				...=yog.getGameVersion();
				...=yog.getGameComment();

				// user code that do something usefull with game infos.
			}
			while (yog.getNextGame());
		}
	*/
	//! Request all game for software gameIdentifier in YOG, return true if there is any game
	bool resetGameLister(void);
	//! Get source nickname from pending game, return NULL when last
	const char *getGameSource(void);
	//! Get program name from pending game, return NULL when last
	const char *getGameIdentifier(void);
	//! Get program version from pending game, return NULL when last
	const char *getGameVersion(void);
	//! Get comment from pending game, return NULL when last
	const char *getGameComment(void);
	//! Returns true and get next game if there is another game in list, false otherwise
	bool getNextGame(void);

private:
	//! Send a string in IRC format
	bool sendString(char *data);
	//! Get a string in IRC format
	bool getString(char data[IRC_MESSAGE_SIZE]);
};

#endif
