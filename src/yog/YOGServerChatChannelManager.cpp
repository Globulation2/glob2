// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "YOGServerChatChannelManager.h"
#include "YOGServerChatChannel.h"
#include "YOGConsts.h"
#include "NetMessage.h"


YOGServerChatChannelManager::YOGServerChatChannelManager()
{
	currentChannelID = LOBBY_CHAT_CHANNEL+1;

	std::shared_ptr<YOGServerChatChannel> newChannel(new YOGServerChatChannel(LOBBY_CHAT_CHANNEL));
	channels.insert(std::make_pair(LOBBY_CHAT_CHANNEL, newChannel));
}



YOGServerChatChannelManager::~YOGServerChatChannelManager()
{

}



void YOGServerChatChannelManager::update()
{
	for(std::map<Uint32, std::shared_ptr<YOGServerChatChannel> >::iterator i = channels.begin(); i!=channels.end();)
	{
		if(i->first != LOBBY_CHAT_CHANNEL)
		{
			if(i->second->getNumberOfPlayers() == 0)
			{
				std::map<Uint32, std::shared_ptr<YOGServerChatChannel> >::iterator i2 = i;
				i++;
				channels.erase(i2);
				continue;
			}
		}
		++i;
	}
}



Uint32 YOGServerChatChannelManager::createNewChatChannel()
{
	//This finds an unused channel ID
	while(channels.find(currentChannelID) != channels.end())
	{
		currentChannelID += 1;
	}
	Uint32 newChannelID = currentChannelID;
	currentChannelID += 1;

	//Creates the channel
	std::shared_ptr<YOGServerChatChannel> newChannel(new YOGServerChatChannel(newChannelID));
	channels.insert(std::make_pair(newChannelID, newChannel));

	return newChannelID;
}



Uint32 YOGServerChatChannelManager::getLobbyChannel()
{
	return LOBBY_CHAT_CHANNEL;
}



std::shared_ptr<YOGServerChatChannel> YOGServerChatChannelManager::getChannel(Uint32 channel)
{
	return channels[channel];
}



