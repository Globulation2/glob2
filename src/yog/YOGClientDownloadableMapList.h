// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <memory>
#include "YOGDownloadableMapInfo.h"
#include <vector>
#include "YOGClientDownloadableMapListener.h"
#include "MapThumbnail.h"
#include "ListenerList.h"

class YOGClient;
class NetMessage;

///This class stores the list of list of downloadable games
class YOGClientDownloadableMapList
{
public:
	///Constructs the map list
	YOGClientDownloadableMapList(YOGClient* client);
	
	///This returns true if the map list is waiting for a responce from the server
	bool waitingForListFromServer();

	///Requests an update to the map list
	void requestMapListUpdate();
	
	///Recieves a message from the server
	void recieveMessage(std::shared_ptr<NetMessage> message);
	
	///Returns the list of downloadable games
	std::vector<YOGDownloadableMapInfo>& getDownloadableMapList();
	
	///Returns a YOGDownloadableMapInfo assocciatted with a given name
	YOGDownloadableMapInfo getMap(const std::string& name);
	
	///Requests a thumbnail for the given map name
	void requestThumbnail(const std::string& name);
	
	///Retrieves the thumbnail for the given map name
	MapThumbnail& getMapThumbnail(const std::string& name);
	
	///Sends a rating about a map
	void submitRating(const std::string& map, Uint8 rating);
	
	///Adds a listener to recieve events when the map list updated
	void addListener(YOGClientDownloadableMapListener* listener);
	
	///Removes a listener from recieving events
	void removeListener(YOGClientDownloadableMapListener* listener);
private:
	///Sends a map list update to the listeners
	void sendUpdateToListeners();
	///Sends a map thumbnail update to the listeners
	void sendThumbnailToListeners();

	std::vector<YOGDownloadableMapInfo> maps;
	std::vector<MapThumbnail> thumbnails;
	YOGClient* client;
	ListenerList<YOGClientDownloadableMapListener> listeners;
	bool waitingForList;
};

