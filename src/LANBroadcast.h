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


#ifndef __LAN_BROADCAST_H
#define __LAN_BROADCAST_H

#include "Header.h"

#ifndef MACOS_OPENTRANSPORT

//Network related includes
#include <sys/types.h>
#include <sys/socket.h>
//#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

//Strange include
#include <unistd.h>

#endif //end of !MACOS_OPENTRANSPORT

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
	Uint16 port;


#ifndef MACOS_OPENTRANSPORT
private:
	hostent *hostEnt;
	sockaddr_in cliAddr;
	sockaddr_in servAddr;
	sockaddr_in senderAddr;
	int socketDefinition;
#endif

};



#endif //end of __LAN_BROADCAST_H

 

