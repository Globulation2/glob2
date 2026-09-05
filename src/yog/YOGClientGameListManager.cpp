// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGClientGameListManager.h"

#include "LobbyMessages.h"
#include "YOGClientGameListListener.h"

using std::static_pointer_cast;

YOGClientGameListManager::YOGClientGameListManager(YOGClient* /*client*/)
{
}



void YOGClientGameListManager::recieveMessage(std::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	
	///This recieves a game list update message
	if(type==MNetUpdateGameList)
	{
		std::shared_ptr<NetUpdateGameList> info = static_pointer_cast<NetUpdateGameList>(message);
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
	listeners.add(listener);
}



void YOGClientGameListManager::removeListener(YOGClientGameListListener* listener)
{
	listeners.remove(listener);
}



void YOGClientGameListManager::sendToListeners()
{
	listeners.notify(&YOGClientGameListListener::gameListUpdated);
}

