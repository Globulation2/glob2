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

static FILE *logClient;
	
struct Message
{
	char text[256];
	int textLength;
	Uint8 messageID;
	Uint8 pad1;
	Uint8 pad2;
	Uint8 pad3;//We need to pad to support memory alignement
	int timeout;
	int TOTL;
	bool received;
	char userName[32];
	int userNameLength;
};

class YOGClient;

struct Game
{
	char name[128];
	YOGClient *host;
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
	
	IPaddress ip;
	UDPsocket socket;
	Uint8 lastSentMessageID; // The last message id that client has sent to YOG. Used to ignore doubles.
	char userName[32];
	
	std::list<Message> messages; // messages to send
	Uint8 lastMessageID; // The last message id sent by YOG to client. Used to give new messages an unique id.
	
	Game *sharingGame;
	
	int timeout;
	int TOTL;
public:
	bool hasip(IPaddress &ip) {return this->ip.host==ip.host && this->ip.port==ip.port;}
};

#endif
