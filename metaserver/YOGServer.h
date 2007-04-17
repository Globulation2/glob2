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

#ifndef __YOG_SERVER_H
#define __YOG_SERVER_H

#include <SDL/SDL.h>
#include <SDL/SDL_endian.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_net.h>
#include <stdio.h>
#include <assert.h>
#include "../src/YOGConsts.h"

#include "YOGClient.h"

//#include <vector>

class YOGServer
{
public:
	YOGServer();
	virtual ~YOGServer();

	bool init();
	void send(IPaddress ip, Uint8 *data, int size);
	void send(IPaddress ip, YOGMessageType v);
	void send(IPaddress ip, YOGMessageType v, Uint8 id);
	void treatPacket(IPaddress ip, Uint8 *data, int size);
	void executeCommand(YOGClient *sender, char *s, Uint8 messageID);
	void removeGame(YOGClient *host);
	void deconnectClient(YOGClient *client);
	void run();
	void lprintf(const char *msg, ...);
	
	std::list<YOGClient *> unconnectedClients;
	std::list<YOGClient *> connectedClients;
	std::list<YOGClient *> authenticatedClients;
	
	UDPsocket socket;
	bool running;
	
	std::list<Game *> games;
	Uint32 gameUID;
	
	FILE *usersFile;
};

#endif
