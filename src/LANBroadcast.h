/*
    Copyright (C) 2001, 2002 Stephane Magnenat & Luc-Olivier de Charriere
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


#ifndef __LAN_BROADCAST_H
#define __LAN_BROADCAST_H

#include "Header.h"

//Network related includes
#include <sys/types.h>
#include <sys/socket.h>
//#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

//Strange include
#include <unistd.h>

class LANBroadcast
{
public:
	LANBroadcast();
	~LANBroadcast();
	bool enable(Uint16 port);
	bool send(int v);
	bool socketReady(void);
	bool receive(int *v);
	Uint32 getSenderIP();

private:
	struct hostent *hostEnt;
	struct sockaddr_in servAddr;
	struct sockaddr_in cliAddr;
	struct sockaddr_in senderAddr;
	Uint16 port;
	int socketDefinition;
};

#endif

 

