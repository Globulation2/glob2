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

#include "YOGClientMapDownloader.h"
#include "YOGClient.h"
#include "NetMessage.h"
#include "YOGClientFileAssembler.h"


YOGClientMapDownloader::YOGClientMapDownloader(boost::shared_ptr<YOGClient> client)
	: client(client)
{
	client->setMapDownloader(this);
}



YOGClientMapDownloader::~YOGClientMapDownloader()
{
	client->setMapDownloader(NULL);
}



void YOGClientMapDownloader::startDownloading(const YOGDownloadableMapInfo& map)
{
	// construct downloader
	fileID = map.getFileID();
	boost::shared_ptr<YOGClientFileAssembler> assembler(new YOGClientFileAssembler(client, fileID));
	assembler->startReceivingFile(map.getMapHeader().getFileName());
	client->setYOGClientFileAssembler(fileID, assembler);
	
	boost::shared_ptr<NetRequestFile> message(new NetRequestFile(fileID));
	client->sendNetMessage(message);
	state = DownloadingMap;
}



void YOGClientMapDownloader::cancelDownload()
{
	if(state == DownloadingMap)
	{
		client->getYOGClientFileAssembler(fileID)->cancelReceivingFile();
		state = Nothing;
	}
}



void YOGClientMapDownloader::recieveMessage(boost::shared_ptr<NetMessage> message)
{

}



void YOGClientMapDownloader::update()
{
	if(client->getYOGClientFileAssembler(fileID)->fileInformationReceived() && state == DownloadingMap && getPercentUploaded()==100)
	{
		state = Finished;
	}
}



YOGClientMapDownloader::DownloadingState YOGClientMapDownloader::getDownloadingState()
{
	return state;
}



int YOGClientMapDownloader::getPercentUploaded()
{
	return client->getYOGClientFileAssembler(fileID)->getPercentage();
}



