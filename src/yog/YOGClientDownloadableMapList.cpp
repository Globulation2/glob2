// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGClientDownloadableMapList.h"
#include "YOGClient.h"
#include "MapDatabaseMessages.h"
#include <SDL.h>
#include <algorithm>

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
	if (waitingForList) return;
	std::shared_ptr<NetRequestDownloadableMapList> request(new NetRequestDownloadableMapList);
	client->sendNetMessage(request);
	waitingForList=true;
}



void YOGClientDownloadableMapList::receiveMessage(std::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetDownloadableMapInfos)
	{
		std::shared_ptr<NetDownloadableMapInfos> info = static_pointer_cast<NetDownloadableMapInfos>(message);
		maps = info->getMaps();
		waitingForList=false;
		sendUpdateToListeners();
	}
	if(type == MNetSendMapThumbnail)
	{
		std::shared_ptr<NetSendMapThumbnail> info = static_pointer_cast<NetSendMapThumbnail>(message);
		for(unsigned int i=0; i<maps.size(); ++i)
		{
			if(maps[i].getMapID() == info->getMapID())
			{
				auto& entry = thumbnailEntry(maps[i]);
				if (!entry.requested) continue;
				entry.image = info->getThumbnail();
				entry.failed = !entry.image.isLoaded();
				entry.requested = false;
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



YOGClientDownloadableMapList::ThumbnailEntry& YOGClientDownloadableMapList::thumbnailEntry(const YOGDownloadableMapInfo& info)
{
	auto header = info.getMapHeader();
	std::string revision(reinterpret_cast<const char*>(header.getGameSHA1()), 20);
	revision += header.getMapName() + ":" + std::to_string(info.getWidth()) + ":" +
		std::to_string(info.getHeight()) + ":" + std::to_string(info.getSize());
	if (!thumbnailCache.count(info.getMapID()) && thumbnailCache.size() >= 32)
	{
		auto oldest = std::min_element(thumbnailCache.begin(), thumbnailCache.end(),
			[](const auto& a, const auto& b) { return a.second.used < b.second.used; });
		thumbnailCache.erase(oldest);
	}
	auto& entry = thumbnailCache[info.getMapID()];
	if (entry.revision != revision) { entry = ThumbnailEntry(); entry.revision = revision; }
	entry.used = ++useCounter;
	return entry;
}

YOGClientDownloadableMapList::ThumbnailState YOGClientDownloadableMapList::getThumbnailState(const std::string& name)
{
	auto& entry = thumbnailEntry(getMap(name));
	if (entry.image.isLoaded()) return ThumbnailState::Ready;
	if (entry.requested && Uint32(SDL_GetTicks() - entry.requestedAt) >= 8000)
		entry.failed = true;
	if (entry.failed) return ThumbnailState::Failed;
	return entry.requested ? ThumbnailState::Loading : ThumbnailState::Empty;
}

void YOGClientDownloadableMapList::requestThumbnail(const std::string& name, bool retry)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			auto& entry = thumbnailEntry(*i);
			if (entry.image.isLoaded() || (!retry && (entry.requested || entry.failed))) return;
			// Even manual retries cannot flood a still-outstanding request.
			if (entry.requested && Uint32(SDL_GetTicks() - entry.requestedAt) < 8000) return;
			entry.requested = true; entry.failed = false; entry.requestedAt = SDL_GetTicks();
			std::shared_ptr<NetRequestMapThumbnail> request(new NetRequestMapThumbnail(i->getMapID()));
			client->sendNetMessage(request);
			return;
		}
	}
}



MapThumbnail& YOGClientDownloadableMapList::getMapThumbnail(const std::string& name)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == name)
		{
			return thumbnailEntry(*i).image;
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
	listeners.add(listener);
}



void YOGClientDownloadableMapList::removeListener(YOGClientDownloadableMapListener* listener)
{
	listeners.remove(listener);
}



void YOGClientDownloadableMapList::sendUpdateToListeners()
{
	listeners.notify(&YOGClientDownloadableMapListener::mapListUpdated);
}


void YOGClientDownloadableMapList::sendThumbnailToListeners()
{
	listeners.notify(&YOGClientDownloadableMapListener::mapThumbnailsUpdated);
}
