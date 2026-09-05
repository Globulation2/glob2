// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "YOGServerChatChannel.h"
#include "YOGServerPlayer.h"
#include "YOGMessage.h"
#include "LobbyMessages.h"


YOGServerChatChannel::YOGServerChatChannel(Uint32 channel)
:	channel(channel)
{

}



void YOGServerChatChannel::addPlayer(std::shared_ptr<YOGServerPlayer> player)
{
	players.push_back(player);
}



void YOGServerChatChannel::removePlayer(std::shared_ptr<YOGServerPlayer> player)
{
	players.remove(player);
}



void YOGServerChatChannel::routeMessage(std::shared_ptr<YOGMessage> message, std::shared_ptr<YOGServerPlayer> sender)
{
	std::shared_ptr<NetSendYOGMessage> netmessage(new NetSendYOGMessage(channel, message));
	for(std::list<std::shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(*i != sender)
			(*i)->sendMessage(netmessage);
	}
}



size_t YOGServerChatChannel::getNumberOfPlayers() const
{
	return players.size();
}

