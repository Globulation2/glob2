/*
  Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charrière
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
	char username[32];
	int usernameLength;
	YOGMessageType messageType;
	Uint8 messageID;
};

struct PrivateReceipt
{
	Uint8 receiptID;
	Uint8 messageID;
	
	//The beginning of the originaly sent message. (e.i. "/m john hello")
	//This allow the original sender to make the difference between the target-UserName and the TextMessage.
	std::list<Uint8> addr;
	
	//A same length list.
	//Bit 1 is true if the target-client is away.
	std::list<bool> away;
	std::list<char *> awayMessages;
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
	static const int DEFAULT_NEW_MESSAGE_TIMEOUT=0;
public:
	YOGClient(IPaddress ip, UDPsocket socket, const char userName[32]);
	virtual ~YOGClient();

	void send(Uint8 *data, int size);
	void send(YOGMessageType v);
	void send(YOGMessageType v, Uint8 id);
	void send(YOGMessageType v, Uint8 *data, int size);
	void send(YOGMessageType v, Uint8 id, Uint8 *data, int size);
	void send(const Message &m);
	void send(PrivateReceipt &privateReceipt);
	
	void sendGames();
	void sendUnshared();
	void addGame(Game *game);
	void addMessage(Message *message);
	void deliveredMessage(Uint8 messageID);
	void addReceipt(PrivateReceipt *privateReceipt);
	void deliveredReceipt(Uint8 receiptID);
	void removeGame(Uint32 uid);
	void removeUselessGames();
	void computeGamesSize();
	void gamesClear();
	
	void sendClients();
	void sendClientsUpdates();
	void addClient(YOGClient *client);
	void removeClient(Uint32 uid);
	void updateClient(Uint32 uid, Uint16 change);
	void deconnected();
	void reconnected(IPaddress ip);
	void newRandomXorPassw();
	void lprintf(const char *msg, ...);
	int strmlen(const char *s, int max);
	
	IPaddress ip;
	IPaddress hostGameip;
	IPaddress joinGameip;
	UDPsocket socket;
	Uint8 lastSentMessageID; // The last message id that client has sent to YOG. Used to ignore doubles.
	char username[32];
	unsigned char password[32];
	unsigned char xorpassw[32];
	std::list<Message> messages; // messages to send
	Uint8 lastMessageID; // The last message id sent by YOG to client. Used to give new messages an unique id.
	int messageTimeout;
	int messageTOTL;
	
	std::list<PrivateReceipt> privateReceipts; // messages to send
	Uint8 lastSentReceiptID;
	int receiptTimeout;
	int receiptTOTL;
	
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
	bool away;
	char awayMessage[64];
public:
	bool hasip(IPaddress &ip) {return this->ip.host==ip.host && this->ip.port==ip.port;}
};

#endif
