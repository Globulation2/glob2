/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charri�e
    for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#ifndef __YOG_CLIENT_H
#define __YOG_CLIENT_H

#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <stdio.h>
#include <assert.h>
#include "../src/YOGConsts.h"
#include <list>

struct Message
{
	char text[256];
	int textLength;
	bool received;
	char userName[32];
	int userNameLength;
	YOGMessageType messageType;
	Uint8 messageID;
	Uint8 pad1;
	Uint8 pad2;
	Uint8 pad3;//We need to pad to support memory alignement
};

class YOGClient;

struct Game
{
	char name[64];
	YOGClient *host;
	Uint32 uid;
	std::list<YOGClient *> joiners; // Firewalls extermination system
};

struct ClientUpdate
{
	Uint32 uid;
	Uint16 change;
};

// This is an metaserver YOG client.
class YOGClient
{

public:
	YOGClient(IPaddress ip, UDPsocket socket, char userName[32]);
	virtual ~YOGClient();

	void send(Uint8 *data, int size);
	void send(YOGMessageType v);
	void send(YOGMessageType v, Uint8 id);
	void send(YOGMessageType v, Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 id, Uint8 *data, int size);
	void send(const Message &m);
	
	void sendGames();
	void sendUnshared();
	void addGame(Game *game);
	void addMessage(Message *message);
	void deleteMessage(Uint8 messageID);
	void removeGame(Uint32 uid);
	void removeUselessGames();
	void computeGamesSize();
	void gamesClear();
	
	void sendClients();
	void sendClientsUpdates();
	void addClient(YOGClient *client);
	void removeClient(Uint32 uid);
	void updateClient(Uint32 uid, bool playing);
	void lprintf(const char *msg, ...);
	int strmlen(const char *s, int max);
	
	IPaddress ip;
	IPaddress hostGameip;
	IPaddress joinGameip;
	UDPsocket socket;
	Uint8 lastSentMessageID; // The last message id that client has sent to YOG. Used to ignore doubles.
	char userName[32];
	std::list<Message> messages; // messages to send
	Uint8 lastMessageID; // The last message id sent by YOG to client. Used to give new messages an unique id.
	int messageTimeout;
	int messageTOTL;
	
	Game *sharingGame;
	Game *joinedGame;
	std::list<YOGClient *> joiners; // Firewalls extermination system
	int joinersTimeout;
	
	std::list<Game *> games;
	int gamesSize;
	int gamesTimeout;
	int gamesTOTL;
	std::list<Uint32> unshared;
	int unsharedTimeout;
	int unsharedTOTL;
	
	std::list<YOGClient *> clients;
	int clientsTimeout;
	int clientsTOTL;
	Uint8 clientsPacketID;
	std::list<YOGClient *> lastSentClients[16];
	
	std::list<ClientUpdate> clientsUpdates;
	int clientsUpdatesTimeout;
	int clientsUpdatesTOTL;
	Uint8 clientsUpdatePacketID;
	std::list<ClientUpdate> lastSentClientsUpdates[16];
	
	int timeout;
	int TOTL;
	
	Uint32 uid;
	bool playing;
public:
	bool hasip(IPaddress &ip) {return this->ip.host==ip.host && this->ip.port==ip.port;}
};

#endif
