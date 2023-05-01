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

#ifndef YOGClientMapDownloader_h
#define YOGClientMapDownloader_h

#include "YOGDownloadableMapInfo.h"
#include "boost/shared_ptr.hpp"
#include <string>

class YOGClient;
class NetMessage;

///This class manages the downloading of a map from the server
class YOGClientMapDownloader
{
public:
	///Constructs a map uploader
	YOGClientMapDownloader(boost::shared_ptr<YOGClient> client);
	
	///Removes the map uploader
	~YOGClientMapDownloader();

	///Starts downloading the given map
	void startDownloading(const YOGDownloadableMapInfo& map);
	
	///If this downloader is downloading a map, this will cancel the download
	void cancelDownload();
	
	///This receives a message from the server
	void receiveMessage(boost::shared_ptr<NetMessage> message);
	
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
	int getPercentUploaded();
private:
	DownloadingState state;
	boost::shared_ptr<YOGClient> client;
	Uint16 fileID;
	std::string mapFile;
};

#endif
