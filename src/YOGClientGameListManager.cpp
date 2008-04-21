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

#include "YOGClientGameListManager.h"

#include "NetMessage.h"
#include "YOGClientGameListListener.h"

YOGClientGameListManager::YOGClientGameListManager(YOGClient* client)
	: client(client)
{
	
}



void YOGClientGameListManager::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	
	///This recieves a game list update message
	if(type==MNetUpdateGameList)
	{
		shared_ptr<NetUpdateGameList> info = static_pointer_cast<NetUpdateGameList>(message);
		info->applyDifferences(games);
		sendToListeners();
	}
}



const std::list<YOGGameInfo>& YOGClientGameListManager::getGameList() const
{
	return games;
}



std::list<YOGGameInfo>& YOGClientGameListManager::getGameList()
{
	return games;
}



YOGGameInfo YOGClientGameListManager::getGameInfo(Uint16 gameID)
{
	for(std::list<YOGGameInfo>::iterator i=games.begin(); i!=games.end(); ++i)
	{
		if(i->getGameID() == gameID)
		{
			return *i;
		}
	}
	return YOGGameInfo();
}



void YOGClientGameListManager::addListener(YOGClientGameListListener* listener)
{
	listeners.push_back(listener);
}



void YOGClientGameListManager::removeListener(YOGClientGameListListener* listener)
{
	listeners.remove(listener);
}



void YOGClientGameListManager::sendToListeners()
{
	for(std::list<YOGClientGameListListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->gameListUpdated();
	}
}

