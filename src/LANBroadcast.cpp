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

#include "LANBroadcast.h"
#include <string.h>

LANBroadcast::LANBroadcast()
{
#ifndef DISABLE_GLOB_LAN_BROADCAST
	socketDefinition=0;
#endif
}

LANBroadcast::~LANBroadcast()
{
#ifndef DISABLE_GLOB_LAN_BROADCAST
	if (socketDefinition)
	{
		close(socketDefinition);
	}
#endif
}

bool LANBroadcast::enable(Uint16 port)
{
	this->port=port;

#ifdef DISABLE_GLOB_LAN_BROADCAST
	return false;
#else
	char *target="255.255.255.255";
	hostEnt = gethostbyname(target);
	if (hostEnt==NULL)
	{
		printf("LANBroadcast::can't get host by name.\n");
		return false;
	}
	servAddr.sin_family=hostEnt->h_addrtype;
	memcpy((char *) &servAddr.sin_addr.s_addr, hostEnt->h_addr_list[0], hostEnt->h_length);
	servAddr.sin_port=htons(port);
	
	//create socket
	socketDefinition=socket(AF_INET, SOCK_DGRAM, 0);
	if (socketDefinition<0)
	{
		printf("LANBroadcast::can't create socket.\n");
		return false;
	}
	
	//bind socket to any port
	cliAddr.sin_family=AF_INET;
	cliAddr.sin_addr.s_addr=htonl(INADDR_ANY);
	cliAddr.sin_port=htons(0);
	for (int i=0; i<8; i++)
		cliAddr.sin_zero[i]=0;
	
	int rc=bind(socketDefinition, (struct sockaddr *) &cliAddr, sizeof(cliAddr));
	//printf("LANBroadcast:: host=(%x), port=(%d).\n", cliAddr.sin_addr.s_addr, cliAddr.sin_port);
	if (rc<0)
	{
		printf("LANBroadcast::can't bind socket.\n");
		close(socketDefinition);
		socketDefinition=0;
		return false;
	}
	
	//INADDR_BROADCAST 
	//setsockopt(sd, SOL_SOCKET, SO_BROADCAST, (char *)&hold, sizeof(hold));
	//setsockopt(sd, IPPROTO_IP, SO_BROADCAST, &hold, sizeof(hold));
	//setsockopt(sd, IPPROTO_IP, SO_BROADCAST, &hold, sizeof(hold));
	
	char ttl=1;
	setsockopt(socketDefinition, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	int hold=1;
	setsockopt(socketDefinition, SOL_SOCKET, SO_BROADCAST, &hold, sizeof(hold));
	
	if (hold!=1)
	{
		printf("LANBroadcast::can't set broadcast socket option.\n");
		close(socketDefinition);
		socketDefinition=0;
		return false;
	}
	
	return true;
#endif
};

bool LANBroadcast::send(int v)
{
#ifdef DISABLE_GLOB_LAN_BROADCAST
	return false;
#else
	char data[4];
	data[0]=v;
	data[1]=0;
	data[2]=0;
	data[3]=0;
	int rc=sendto(socketDefinition, data, 4, 0, (struct sockaddr *) &servAddr, sizeof(servAddr));
	
	if (rc<0)
	{
		printf("LANBroadcast::unable to send data.\n");
		return false;
	}
	
	printf("LANBroadcast::data sent sucessfully v=(%d).\n", v);
	return true;
#endif
}

bool LANBroadcast::socketReady()
{
#ifdef DISABLE_GLOB_LAN_BROADCAST
	return false;
#else
	if (socketDefinition==0)
		return false;
	
	int retval = 0;
	struct timeval tv;
	fd_set mask;
	
	// Set up the mask of file descriptors
	FD_ZERO(&mask);
	FD_SET(socketDefinition, &mask);
	
	// Set up the timeout: we don't wait
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	
	
	retval = select(socketDefinition+1, &mask, NULL, NULL, &tv);
	
	return(retval == 1);
#endif
}

bool LANBroadcast::receive(int *v, char gameName[32], char serverNickName[32])
{
#ifdef DISABLE_GLOB_LAN_BROADCAST
	return false;
#else
	if (socketDefinition==0)
		return false;
	if (!socketReady())
		return false;
	
	char data[68];
	socklen_t senderLen;
	//printf("sizeof(senderAddr)=%d.\n", sizeof(senderAddr));
	//printf("sizeof(sockaddr)=%d.\n", sizeof(sockaddr));
	senderLen = sizeof(senderAddr);
	int n=recvfrom(socketDefinition, data, 68, 0, (struct sockaddr *) &senderAddr, &senderLen);
	//printf("senderLen=%d.\n", senderLen);
	
	if (n<0)
	{
		printf("LANBroadcast::can't recieve data!.\n");
		return false;
	}
	
	if (n<68)
	{
		printf("LANBroadcast::bad size for a presence response (%d).\n", n);
		return false;
	}
	if (gameName)
		strncpy(gameName, &data[4], 32);
	if (serverNickName)
		strncpy(serverNickName, &data[36], 32);
	
	//printf("senderLen=%d, n=%d\n", senderLen, n);
	
	//printf("LANBroadcast::senderAddr=(%d, %x, %d).\n",
	//	senderAddr.sin_family, senderAddr.sin_addr.s_addr, senderAddr.sin_port);
	
	//printf("LANBroadcast::data1 recieved (%d, %d, %d, %d).\n", data[0], data[1], data[2], data[3]);
	//printf("LANBroadcast::dataA recieved (%d, %d, %d, %d).\n", data[4], data[5], data[6], data[7]);
	//printf("LANBroadcast::data2 recieved (%s).\n", &data[4]);
	printf("LANBroadcast::dataB recieved gameName=(%s) serverNickName=(%s).\n", gameName, serverNickName);
	
	if (v)
		*v=data[0];
	return true;
#endif
}

Uint32 LANBroadcast::getSenderIP()
{
#ifdef DISABLE_GLOB_LAN_BROADCAST
	return 0;
#else
	return senderAddr.sin_addr.s_addr;
#endif
}
