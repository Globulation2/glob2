// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGClientMapDownloader.h"
#include "YOGClient.h"
#include "FileTransferMessages.h"
#include "YOGClientFileAssembler.h"


YOGClientMapDownloader::YOGClientMapDownloader(std::shared_ptr<YOGClient> client)
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
	std::shared_ptr<YOGClientFileAssembler> assembler(new YOGClientFileAssembler(client, fileID));
	assembler->startRecievingFile(map.getMapHeader().getFileName());
	client->setYOGClientFileAssembler(fileID, assembler);
	
	std::shared_ptr<NetRequestFile> message(new NetRequestFile(fileID));
	client->sendNetMessage(message);
	state = DownloadingMap;
}



void YOGClientMapDownloader::cancelDownload()
{
	if(state == DownloadingMap)
	{
		client->getYOGClientFileAssembler(fileID)->cancelRecievingFile();
		state = Nothing;
	}
}



void YOGClientMapDownloader::recieveMessage(std::shared_ptr<NetMessage> message)
{

}



void YOGClientMapDownloader::update()
{
	if(client->getYOGClientFileAssembler(fileID)->fileInformationRecieved() && state == DownloadingMap && getPercentDownloaded()==100)
	{
		state = Finished;
	}
}



YOGClientMapDownloader::DownloadingState YOGClientMapDownloader::getDownloadingState()
{
	return state;
}



int YOGClientMapDownloader::getPercentDownloaded()
{
	return client->getYOGClientFileAssembler(fileID)->getPercentage();
}



