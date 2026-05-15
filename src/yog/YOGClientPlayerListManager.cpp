// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGClientPlayerListManager.h"
#include "YOGClientPlayerListListener.h"
#include "LobbyMessages.h"

using std::static_pointer_cast;

YOGClientPlayerListManager::YOGClientPlayerListManager(YOGClient* /*client*/)
{
}



void YOGClientPlayerListManager::recieveMessage(std::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type==MNetUpdatePlayerList)
	{
		std::shared_ptr<NetUpdatePlayerList> info = static_pointer_cast<NetUpdatePlayerList>(message);
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



