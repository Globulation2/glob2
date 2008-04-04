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

#include "P2PManager.h"
#include "YOGServerPlayer.h"
#include "NetMessage.h"


P2PManager::P2PManager()
{

}


void P2PManager::addPlayer(boost::shared_ptr<YOGServerPlayer> player)
{
	players.push_back(player);
	P2PPlayerInformation info;
	info.setIPAddress(player->getPlayerIP());
	info.setPort(player->getP2PPort());
	info.setPlayerID(player->getPlayerID());
	group.addP2PPlayer(info);
	
	boost::shared_ptr<NetSendP2PInformation> message(new NetSendP2PInformation(group));
	sendNetMessage(message);
	
	player->setP2PManager(this);
}



void P2PManager::removePlayer(boost::shared_ptr<YOGServerPlayer> player)
{
	int pos = std::find(players.begin(), players.end(), player) - players.begin();
	players.erase(players.begin() + pos);
	group.removeP2PPlayer(pos);
	
	boost::shared_ptr<NetSendP2PInformation> message(new NetSendP2PInformation(group));
	sendNetMessage(message);
	
	player->setP2PManager(NULL);
}



void P2PManager::update()
{
	for(std::vector<boost::shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(!(*i)->isConnected())
		{
			int pos = i - players.begin();
			removePlayer(*i);
			i = players.begin() + pos;
		}
	}
}



void P2PManager::recieveMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player)
{
	Uint8 type = message->getMessageType();
	if(type == MNetSendOrder)
	{
		sendNetMessage(message, player);
	}
}



void P2PManager::sendNetMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player)
{
	for(std::vector<boost::shared_ptr<YOGServerPlayer> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(*i != player)
			(*i)->sendMessage(message);
	}
}

