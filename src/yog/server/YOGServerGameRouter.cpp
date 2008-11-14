/*
  Copyright (C) 2008 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "YOGServerGameRouter.h"
#include "YOGServerRouterPlayer.h"
#include "NetMessage.h"


YOGServerGameRouter::YOGServerGameRouter()
{

}



void YOGServerGameRouter::addPlayer(boost::shared_ptr<YOGServerRouterPlayer> player)
{
	players.push_back(player);
}



void YOGServerGameRouter::update()
{
	for(std::vector<boost::shared_ptr<YOGServerRouterPlayer> >::iterator i=players.begin(); i!=players.end();)
	{
		if(!(*i)->isConnected())
		{
			Uint32 n = i - players.begin();
			players.erase(i);
			i = players.begin() + n;
		}
		else
		{
			++i;
		}
	}
}



bool YOGServerGameRouter::isEmpty()
{
	if(players.empty())
		return true;
	return false;
}



void YOGServerGameRouter::routeMessage(boost::shared_ptr<NetMessage> message, YOGServerRouterPlayer* sender)
{
	for(std::vector<boost::shared_ptr<YOGServerRouterPlayer> >::iterator i=players.begin(); i!=players.end(); ++i)
	{
		if(i->get() != sender)
		{
			(*i)->sendNetMessage(message);
		}
	}
}

