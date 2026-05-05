// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <tuple>
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
	
	///Removes a map from the database
	void removeMap(const std::string& map);
	
	///Returns true if there is a map by this map name
	bool doesMapExist(const std::string& map);

	///Returns whether the given map can be obtained from the player, returns YOGMapUploadReasonUnknown
	///if it it can be recieved
	YOGMapUploadRefusalReason canRecieveFromPlayer(const YOGDownloadableMapInfo& map);

	///Starts recieving a map from the given player, and returns the file ID for the transfer
	Uint16 recieveMapFromPlayer(const YOGDownloadableMapInfo& map, std::shared_ptr<YOGServerPlayer> player);
	
	///Sends the list of maps to the given player
	void sendMapListToPlayer(std::shared_ptr<YOGServerPlayer> player);
	
	///Sends a map thumbnail to the given player
	void sendMapThumbnailToPlayer(Uint16 mapID, std::shared_ptr<YOGServerPlayer> player);
	
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
	std::vector<std::tuple<YOGDownloadableMapInfo, int> > uploadingMaps;
};

