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

#include "YOGClientPlayerListManager.h"
#include "YOGClientPlayerListListener.h"
#include "NetMessage.h"

YOGClientPlayerListManager::YOGClientPlayerListManager(YOGClient* client)
	: client(client)
{

}



void YOGClientPlayerListManager::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type==MNetUpdatePlayerList)
	{
		shared_ptr<NetUpdatePlayerList> info = static_pointer_cast<NetUpdatePlayerList>(message);
		info->applyDifferences(players);
		sendToListeners();
	}
}



const std::list<YOGPlayerSessionInfo>& YOGClientPlayerListManager::getPlayerList() const
{
	return players;
}



std::list<YOGPlayerSessionInfo>& YOGClientPlayerListManager::getPlayerList()
{
	return players;
}



void YOGClientPlayerListManager::addListener(YOGClientPlayerListListener* listener)
{
	listeners.push_back(listener);
}



void YOGClientPlayerListManager::removeListener(YOGClientPlayerListListener* listener)
{
	listeners.remove(listener);
}



std::string YOGClientPlayerListManager::findPlayerName(Uint16 playerID)
{
	for(std::list<YOGPlayerSessionInfo>::iterator i = players.begin(); i != players.end(); ++i)
	{
		if(i->getPlayerID() == playerID)
			return i->getPlayerName();
	}
	return "";
}



bool YOGClientPlayerListManager::doesPlayerExist(const std::string& name)
{
	for(std::list<YOGPlayerSessionInfo>::iterator i = players.begin(); i != players.end(); ++i)
	{
		if(i->getPlayerName() == name)
			return true;
	}
	return false;
}



YOGPlayerSessionInfo& YOGClientPlayerListManager::getPlayerInfo(const std::string& name)
{
	for(std::list<YOGPlayerSessionInfo>::iterator i = players.begin(); i != players.end(); ++i)
	{
		if(i->getPlayerName() == name)
			return *i;
	}
	assert(false);
}



void YOGClientPlayerListManager::sendToListeners()
{
	for(std::list<YOGClientPlayerListListener*>::iterator i = listeners.begin(); i != listeners.end(); ++i)
	{
		(*i)->playerListUpdated();
	}
}



