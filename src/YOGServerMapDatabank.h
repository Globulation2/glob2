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

#ifndef YOGServerMapDatabank_h
#define YOGServerMapDatabank_h

#include "boost/tuple/tuple.hpp"
#include "MapThumbnail.h"
#include <vector>
#include "YOGDownloadableMapInfo.h"
#include "YOGServerPlayer.h"

class YOGServer;

///This stores maps that users can download on the server end
class YOGServerMapDatabank
{
public:
	///Constructs the class, loading the databank
	YOGServerMapDatabank(YOGServer* server);

	///Adds a map to the database
	void addMap(const YOGDownloadableMapInfo& map);

	///Returns whether the given map can be obtained from the player, returns YOGMapUploadReasonUnknown
	///if it it can be recieved
	YOGMapUploadRefusalReason canRecieveFromPlayer(const YOGDownloadableMapInfo& map);

	///Starts recieving a map from the given player, and returns the file ID for the transfer
	Uint16 recieveMapFromPlayer(const YOGDownloadableMapInfo& map, boost::shared_ptr<YOGServerPlayer> player);
	
	///Sends the list of maps to the given player
	void sendMapListToPlayer(boost::shared_ptr<YOGServerPlayer> player);
	
	///Sends a map thumbnail to the given player
	void sendMapThumbnailToPlayer(Uint16 mapID, boost::shared_ptr<YOGServerPlayer> player);
	
	///Submits a rating for a given player.
	void submitRating(Uint16 mapID, Uint8 rating);
	
	///This updates the map databank
	void update();
private:
	friend class YOGServer;
	///Returns the file that the compressed thumbnail information would be stored in
	std::string getThumbtackFile(const std::string& mapName);
	///This loads the thumbnail, either from a file or generating it on the fly
	MapThumbnail loadThumbnail(const std::string& mapName, const std::string& fileName);

	///This does a full load of the map databank
	void load();
	///This does a full save of the map databank
	void save();
	
	Uint16 currentMapID;
	
	YOGServer* server;
	
	std::vector<YOGDownloadableMapInfo> maps;
	///List of maps currently being uploaded
	std::vector<boost::tuple<YOGDownloadableMapInfo, int> > uploadingMaps;
};

#endif
