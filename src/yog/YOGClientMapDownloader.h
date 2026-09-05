// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "YOGDownloadableMapInfo.h"
#include <memory>
#include <string>

class YOGClient;
class NetMessage;

///This class manages the downloading of a map from the server
class YOGClientMapDownloader
{
public:
	///Constructs a map uploader
	YOGClientMapDownloader(std::shared_ptr<YOGClient> client);
	
	///Removes the map uploader
	~YOGClientMapDownloader();

	///Starts downloading the given map
	void startDownloading(const YOGDownloadableMapInfo& map);
	
	///If this downloader is downloading a map, this will cancel the download
	void cancelDownload();
	
	///This recieves a message from the server
	void recieveMessage(std::shared_ptr<NetMessage> message);
	
	///This updates the downloader
	void update();

	enum DownloadingState
	{
		Nothing,
		DownloadingMap,
		Finished,
	};
	///Returns the current downloading state
	DownloadingState getDownloadingState();
	
	///Returns the percent downloaded
	int getPercentDownloaded();
private:
	DownloadingState state;
	std::shared_ptr<YOGClient> client;
	Uint16 fileID;
	std::string mapFile;
};

