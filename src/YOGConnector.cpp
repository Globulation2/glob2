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

#include <string.h>
#include <stdio.h>
#include "YOGConnector.h"
#include "GlobalContainer.h"

YOGConnector::YOGConnector()
{
	socket=NULL;
	socketSet=NULL;
}

YOGConnector::~YOGConnector()
{

}

void YOGConnector::open(void)
{
	IPaddress ip;
	socketSet=SDLNet_AllocSocketSet(1);
	if(SDLNet_ResolveHost(&ip, globalContainer->metaServerName, globalContainer->metaServerPort)==-1)
	{
		fprintf(stderr, "YOG : ResolveHost: %s\n", SDLNet_GetError());
		return;
	}

	socket=SDLNet_TCP_Open(&ip);
	if(!socket)
	{
		fprintf(stderr, "YOG : TCP_Open: %s\n", SDLNet_GetError());
		return;
	}

	SDLNet_TCP_AddSocket(socketSet, socket);
}

void YOGConnector::close(void)
{
	if (socket)
	{
		SDLNet_TCP_Close(socket);
		socket=NULL;
		SDLNet_FreeSocketSet(socketSet);
	}
}

bool YOGConnector::getString(char data[GAME_INFO_MAX_SIZE])
{
	if (socket)
	{
		int i;
		int value;
		char c;

		i=0;
		while ( (  (value=SDLNet_TCP_Recv(socket, &c, 1)) >0) && (i<GAME_INFO_MAX_SIZE-1))
		{
			if ((c==0) || (c=='\n') || (c=='\r'))
			{
				break;
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

bool YOGConnector::sendString(char *data)
{
	if (socket)
	{
		int len=strlen(data)+1; // add one for the terminating NULL
		int result=SDLNet_TCP_Send(socket, data, len);
		return (result==len);
	}
	else
	{
		return false;
	}
}

