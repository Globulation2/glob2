// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "YOGClientChatChannel.h"
#include "YOGClient.h"
#include "YOGMessage.h"
#include "YOGClientChatListener.h"
#include "NetMessage.h"

YOGClientChatChannel::YOGClientChatChannel(Uint32 channelID, std::shared_ptr<YOGClient> client)
	: client(client), channelID(channelID)
{
	client->addYOGClientChatChannel(this);
}



YOGClientChatChannel::~YOGClientChatChannel()
{
	client->removeYOGClientChatChannel(this);
}



Uint32 YOGClientChatChannel::getHistorySize() const
{
	return messageHistory.size();
}



const std::shared_ptr<YOGMessage> YOGClientChatChannel::getMessage(Uint32 n) const
{
	return std::get<0>(messageHistory[n]);
}



boost::posix_time::ptime YOGClientChatChannel::getMessageTime(Uint32 n) const
{
	return std::get<1>(messageHistory[n]);
}



void YOGClientChatChannel::sendMessage(std::shared_ptr<YOGMessage> message)
{
	if(channelID != static_cast<Uint32>(-1))
	{
		messageHistory.push_back(std::make_tuple(message, boost::posix_time::second_clock::local_time()));
		std::shared_ptr<NetSendYOGMessage> netmessage(new NetSendYOGMessage(channelID, message));
		client->sendNetMessage(netmessage);
		sendToListeners(message);
	}
}



Uint32 YOGClientChatChannel::getChannelID() const
{
	return channelID;
}



void YOGClientChatChannel::setChannelID(Uint32 channel)
{
	client->removeYOGClientChatChannel(this);
	channelID = channel;
	client->addYOGClientChatChannel(this);
}



void YOGClientChatChannel::addListener(YOGClientChatListener* listener)
{
	listeners.push_back(listener);
}



void YOGClientChatChannel::removeListener(YOGClientChatListener* listener)
{
	listeners.remove(listener);
}



void YOGClientChatChannel::recieveMessage(std::shared_ptr<YOGMessage> message)
{
	messageHistory.push_back(std::make_tuple(message, boost::posix_time::second_clock::local_time()));
	sendToListeners(message);
}



void YOGClientChatChannel::sendToListeners(std::shared_ptr<YOGMessage> message)
{
	for(std::list<YOGClientChatListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->recieveTextMessage(message);
	}
}


