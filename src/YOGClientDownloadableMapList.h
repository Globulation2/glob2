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

#ifndef YOGClientDownloadableMapList_h
#define YOGClientDownloadableMapList_h

#include "boost/shared_ptr.hpp"
#include "YOGDownloadableMapInfo.h"
#include <vector>
#include <list>
#include "YOGClientDownloadableMapListener.h"
#include "MapThumbnail.h"

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
	void recieveMessage(boost::shared_ptr<NetMessage> message);
	
	///Returns the list of downloadable games
	std::vector<YOGDownloadableMapInfo>& getDownloadableMapList();
	
	///Returns a YOGDownloadableMapInfo assocciatted with a given name
	YOGDownloadableMapInfo getMap(const std::string& name);
	
	///Requests a thumbnail for the given map name
	void requestThumbnail(const std::string& name);
	
	///Retrieves the thumbnail for the given map name
	MapThumbnail& getMapThumbnail(const std::string& name);
	
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
	std::list<YOGClientDownloadableMapListener*> listeners;
	bool waitingForList;
};

#endif
