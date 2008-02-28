/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#include "YOGServerChatChannel.h"
#include "YOGPlayer.h"
#include "YOGMessage.h"
#include "NetMessage.h"


YOGServerChatChannel::YOGServerChatChannel(Uint32 channel)
:	channel(channel)
{

}



void YOGServerChatChannel::addPlayer(boost::shared_ptr<YOGPlayer> player)
{
	players.push_back(player);
}



void YOGServerChatChannel::removePlayer(boost::shared_ptr<YOGPlayer> player)
{
	players.remove(player);
}



void YOGServerChatChannel::routeMessage(boost::shared_ptr<YOGMessage> message, boost::shared_ptr<YOGPlayer> sender)
{
	boost::shared_ptr<NetSendYOGMessage> netmessage(new NetSendYOGMessage(channel, message));
	for(std::list<boost::shared_ptr<YOGPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(*i != sender)
			(*i)->sendMessage(netmessage);
	}
}



size_t YOGServerChatChannel::getNumberOfPlayers() const
{
	return players.size();
}

