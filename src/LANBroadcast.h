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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef macintosh
#define DISABLE_GLOB_LAN_BROADCAST
#endif


// Include system network headers
#ifdef DISABLE_GLOB_LAN_BROADCAST


#else
#	if defined(__WIN32__) || defined(WIN32)
//#		define Win32_Winsock
//#		include <windows.h>
#	else // UNIX
#			include <sys/types.h>
#			include <sys/socket.h>
//#			include <netinet/in.h>
#			include <arpa/inet.h>
#			include <netdb.h>
			//Strange include:
#			include <unistd.h>
#		ifndef __BEOS__
//#			include <arpa/inet.h>
#		endif
#		ifdef linux
//#			include <netinet/tcp.h>
#		endif
#	include <netdb.h>
#	include <sys/socket.h>
#	endif
#endif


class LANBroadcast
{
public:
	LANBroadcast();
	~LANBroadcast();
	bool enable(unsigned short port);
	bool send(int v);
	bool socketReady(void);
	//! fill v, gameName, and serverNickName, if not NULL, with last received broadcast response.
	bool receive(int *v, char gameName[32], char serverNickName[32]);
	unsigned int getSenderIP();

private:
	unsigned short port;


#ifndef DISABLE_GLOB_LAN_BROADCAST
private:
	hostent *hostEnt;
	struct sockaddr_in cliAddr;
	struct sockaddr_in servAddr;
	struct sockaddr_in senderAddr;
	int socketDefinition;
#endif

};



#endif //end of __LAN_BROADCAST_H

 

