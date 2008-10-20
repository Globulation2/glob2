/*
  Copyright 2007 Bradley Arsenault

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

#include "YOGServerMapDistributor.h"

#include "YOGServerGame.h"
#include "YOGServerPlayer.h"
#include "NetMessage.h"

using namespace boost;

YOGServerMapDistributor::YOGServerMapDistributor(boost::shared_ptr<YOGServerGame> game, boost::shared_ptr<YOGServerPlayer> host)
	: sentRequest(false), game(game), host(host)
{

}



void YOGServerMapDistributor::update()
{
	for(std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, unsigned, int> >::iterator i = players.begin(); i!=players.end();)
	{
		if(!i->get<0>()->isConnected())
		{
			i = players.erase(i);
			continue;
		}

		if(i->get<2>() == 0 && fileInfo)
		{
			i->get<0>()->sendMessage(fileInfo);
			i->get<2>() = 1;
		}
		if(i->get<2>() == 1 && (i->get<1>() < chunks.size()))
		{
			i->get<0>()->sendMessage(chunks[i->get<1>()]);
			i->get<2>() = 2;
			i->get<1>() += 1;
		}
		++i;
	}
}



void YOGServerMapDistributor::addMapRequestee(boost::shared_ptr<YOGServerPlayer> player)
{
	if(!sentRequest)
	{
		shared_ptr<NetRequestMap> message(new NetRequestMap);
		host->sendMessage(message);
		sentRequest=true;
	}
	players.push_back(boost::make_tuple(player, 0, 0));
}



void YOGServerMapDistributor::removeMapRequestee(boost::shared_ptr<YOGServerPlayer> player)
{
	for(std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, unsigned, int> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(i->get<0>() == player)
		{
			players.erase(i);
			return;
		}
	}
}


	
void YOGServerMapDistributor::handleMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player)
{
	///This ignores certain messages that must come from the host
	Uint8 messageType = message->getMessageType();
	if(messageType == MNetSendFileInformation && host == player)
	{
		fileInfo = static_pointer_cast<NetSendFileInformation>(message);
		shared_ptr<NetRequestNextChunk> message(new NetRequestNextChunk);
		host->sendMessage(message);
	}
	else if(messageType == MNetSendFileChunk && host == player)
	{
		chunks.push_back(static_pointer_cast<NetSendFileChunk>(message));
		shared_ptr<NetRequestNextChunk> message(new NetRequestNextChunk);
		host->sendMessage(message);
	}
	else if(messageType == MNetRequestNextChunk)
	{
		for(std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, unsigned, int> >::iterator i = players.begin(); i!=players.end(); ++i)
		{
			if(i->get<0>() == player)
			{
				i->get<2>() = 1;
			}
		}
	}
}


