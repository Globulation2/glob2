/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "YOGChatChannel.h"
#include "YOGClient.h"
#include "YOGMessage.h"
#include "YOGChatListener.h"
#include "NetMessage.h"

YOGChatChannel::YOGChatChannel(Uint32 channelID, boost::shared_ptr<YOGClient> client)
	: channelID(channelID), client(client)
{
	client->addYOGChatChannel(this);
}



YOGChatChannel::~YOGChatChannel()
{
	client->removeYOGChatChannel(this);
}



Uint32 YOGChatChannel::getHistorySize() const
{
	return messageHistory.size();
}



const boost::shared_ptr<YOGMessage> YOGChatChannel::getMessage(Uint32 n) const
{
	return messageHistory[n].get<0>();
}



boost::posix_time::ptime YOGChatChannel::getMessageTime(Uint32 n) const
{
	return messageHistory[n].get<1>();
}



void YOGChatChannel::sendMessage(boost::shared_ptr<YOGMessage> message)
{
	if(channelID != -1)
	{
		messageHistory.push_back(boost::make_tuple(message, boost::posix_time::second_clock::local_time()));
		boost::shared_ptr<NetSendYOGMessage> netmessage(new NetSendYOGMessage(channelID, message));
		client->sendNetMessage(netmessage);
		sendToListeners(message);
	}
}



Uint32 YOGChatChannel::getChannelID() const
{
	return channelID;
}



void YOGChatChannel::setChannelID(Uint32 channel)
{
	client->removeYOGChatChannel(this);
	channelID = channel;
	client->addYOGChatChannel(this);
}



void YOGChatChannel::addListener(YOGChatListener* listener)
{
	listeners.push_back(listener);
}



void YOGChatChannel::removeListener(YOGChatListener* listener)
{
	listeners.remove(listener);
}



void YOGChatChannel::recieveMessage(boost::shared_ptr<YOGMessage> message)
{
	messageHistory.push_back(boost::make_tuple(message, boost::posix_time::second_clock::local_time()));
	sendToListeners(message);
}



void YOGChatChannel::sendToListeners(boost::shared_ptr<YOGMessage> message)
{
	for(std::list<YOGChatListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->recieveTextMessage(message);
	}
}


