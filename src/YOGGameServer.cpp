/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "YOGGameServer.h"


YOGGameServer::YOGGameServer()
{
	nl.startListening(YOG_SERVER_PORT);
}



void YOGGameServer::update()
{
	//First attempt connections with new players
	shared_ptr<NetConnection> nc = new NetConnection;
	while(nl.attemptConnection(*nc))
	{
		players.pushBack(YOGPlayer(nc));
		nc.reset(new NetConnection);
	}

	//Call update to all of the players
	for(std::list<YOGPlayer>::iterator i=players.begin(); i!=players.end(); ++i)
	{
		i->update();
	}

	//Remove all of the players that have disconnected.
	for(std::list<YOGPlayer>::iterator i=players.begin(); i!=players.end();)
	{
		if(!i->isConnected())
		{
			i = players.erase(i);
		}
		else
		{
			i++;
		}
	}
}



