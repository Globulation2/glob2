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

#include "YOGChatChannelManager.h"
#include "YOGConsts.h"


YOGChatChannelManager::YOGChatChannelManager()
{
	currentChannelID = LOBBY_CHAT_CHANNEL+1;

	boost::shared_ptr<YOGServerChatChannel> newChannel(new YOGServerChatChannel(LOBBY_CHAT_CHANNEL));
	channels.insert(std::make_pair(LOBBY_CHAT_CHANNEL, newChannel));
}



YOGChatChannelManager::~YOGChatChannelManager()
{

}



void YOGChatChannelManager::update()
{
	for(std::map<Uint32, boost::shared_ptr<YOGServerChatChannel> >::iterator i = channels.begin(); i!=channels.end();)
	{
		if(i->first != LOBBY_CHAT_CHANNEL)
		{
			if(i->second->getNumberOfPlayers() == 0)
			{
				std::map<Uint32, boost::shared_ptr<YOGServerChatChannel> >::iterator i2 = i;
				i++;
				channels.erase(i2);
				continue;
			}
		}
		++i;
	}
}



Uint32 YOGChatChannelManager::createNewChatChannel()
{
	//This finds an unused channel ID
	while(channels.find(currentChannelID) != channels.end())
	{
		currentChannelID += 1;
	}
	Uint32 newChannelID = currentChannelID;
	currentChannelID += 1;

	//Creates the channel
	boost::shared_ptr<YOGServerChatChannel> newChannel(new YOGServerChatChannel(newChannelID));
	channels.insert(std::make_pair(newChannelID, newChannel));

	return newChannelID;
}



Uint32 YOGChatChannelManager::getLobbyChannel()
{
	return LOBBY_CHAT_CHANNEL;
}



boost::shared_ptr<YOGServerChatChannel> YOGChatChannelManager::getChannel(Uint32 channel)
{
	return channels[channel];
}



