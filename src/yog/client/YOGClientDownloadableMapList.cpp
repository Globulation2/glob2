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


#include "YOGClientDownloadableMapList.h"
#include "YOGClient.h"
#include "NetMessage.h"


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
	boost::shared_ptr<NetRequestDownloadableMapList> request(new NetRequestDownloadableMapList);
	client->sendNetMessage(request);
	waitingForList=true;
}



void YOGClientDownloadableMapList::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetDownloadableMapInfos)
	{
		boost::shared_ptr<NetDownloadableMapInfos> info = static_pointer_cast<NetDownloadableMapInfos>(message);
		maps = info->getMaps();
		thumbnails.resize(maps.size());
		sendUpdateToListeners();
		waitingForList=false;
	}
	if(type == MNetSendMapThumbnail)
	{
		boost::shared_ptr<NetSendMapThumbnail> info = static_pointer_cast<NetSendMapThumbnail>(message);
		for(int i=0; i<maps.size(); ++i)
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
}



void YOGClientDownloadableMapList::requestThumbnail(const std::string& name)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			boost::shared_ptr<NetRequestMapThumbnail> request(new NetRequestMapThumbnail(i->getMapID()));
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
}



void YOGClientDownloadableMapList::submitRating(const std::string& name, Uint8 rating)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			boost::shared_ptr<NetSubmitRatingOnMap> request(new NetSubmitRatingOnMap(i->getMapID(), rating));
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

