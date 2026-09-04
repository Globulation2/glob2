// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGClientDownloadableMapList.h"
#include "YOGClient.h"
#include "NetMessage.h"

using std::static_pointer_cast;

YOGClientDownloadableMapList::YOGClientDownloadableMapList(YOGClient* client)
	: client(client)
{
	waitingForList=false;
}



bool YOGClientDownloadableMapList::waitingForListFromServer()
{
	return waitingForList;
}



void YOGClientDownloadableMapList::requestMapListUpdate()
{
	maps.clear();
	thumbnails.clear();
	std::shared_ptr<NetRequestDownloadableMapList> request(new NetRequestDownloadableMapList);
	client->sendNetMessage(request);
	waitingForList=true;
}



void YOGClientDownloadableMapList::recieveMessage(std::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetDownloadableMapInfos)
	{
		std::shared_ptr<NetDownloadableMapInfos> info = static_pointer_cast<NetDownloadableMapInfos>(message);
		maps = info->getMaps();
		thumbnails.resize(maps.size());
		sendUpdateToListeners();
		waitingForList=false;
	}
	if(type == MNetSendMapThumbnail)
	{
		std::shared_ptr<NetSendMapThumbnail> info = static_pointer_cast<NetSendMapThumbnail>(message);
		for(unsigned int i=0; i<maps.size(); ++i)
		{
			if(maps[i].getMapID() == info->getMapID())
			{
				thumbnails[i] = info->getThumbnail();
			}
		}
		sendThumbnailToListeners();
	}
}



std::vector<YOGDownloadableMapInfo>& YOGClientDownloadableMapList::getDownloadableMapList()
{
	return maps;
}



YOGDownloadableMapInfo YOGClientDownloadableMapList::getMap(const std::string& name)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			return *i;
		}
	}
	assert(false);
//	return YOGDownloadableMapInfo();//to satisfy -Wall
}



void YOGClientDownloadableMapList::requestThumbnail(const std::string& name)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			std::shared_ptr<NetRequestMapThumbnail> request(new NetRequestMapThumbnail(i->getMapID()));
			client->sendNetMessage(request);
		}
	}
}



MapThumbnail& YOGClientDownloadableMapList::getMapThumbnail(const std::string& name)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			return thumbnails[i - maps.begin()];
		}
	}
	assert(false);
//	return thumbnails[0];//to satisfy -Wall
}



void YOGClientDownloadableMapList::submitRating(const std::string& name, Uint8 rating)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			std::shared_ptr<NetSubmitRatingOnMap> request(new NetSubmitRatingOnMap(i->getMapID(), rating));
			client->sendNetMessage(request);
			i->setNumberOfRatings(i->getNumberOfRatings() + 1);
			i->setRatingTotal(i->getRatingTotal() + rating);
		}
	}
}



void YOGClientDownloadableMapList::addListener(YOGClientDownloadableMapListener* listener)
{
	listeners.push_back(listener);
}



void YOGClientDownloadableMapList::removeListener(YOGClientDownloadableMapListener* listener)
{
	listeners.remove(listener);
}



void YOGClientDownloadableMapList::sendUpdateToListeners()
{
	for(std::list<YOGClientDownloadableMapListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->mapListUpdated();
	}
}


void YOGClientDownloadableMapList::sendThumbnailToListeners()
{
	for(std::list<YOGClientDownloadableMapListener*>::iterator i = listeners.begin(); i!=listeners.end(); ++i)
	{
		(*i)->mapThumbnailsUpdated();
	}
}

