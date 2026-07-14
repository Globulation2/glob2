// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "Engine.h"
#include "MapUploadMessages.h"
#include "YOGClientFileAssembler.h"
#include "YOGClient.h"
#include "YOGClientMapUploader.h"
#include "YOGConsts.h"

#include "Toolkit.h"
#include "FileManager.h"
#include "BinaryStream.h"

using std::static_pointer_cast;

YOGClientMapUploader::YOGClientMapUploader(std::shared_ptr<YOGClient> client)
	: state(Nothing), client(client)
{
	client->setMapUploader(this);
	fileID=0;
	reason=YOGMapUploadReasonUnknown;
}



YOGClientMapUploader::~YOGClientMapUploader()
{
	client->setMapUploader(NULL);
}



void YOGClientMapUploader::startUploading(const std::string& nmapFile, const std::string& newMapName, const std::string& authorName, int w, int h)
{
	mapFile = nmapFile;
	Engine engine;
	YOGDownloadableMapInfo info;
	MapHeader header = engine.loadMapHeader(mapFile);
	header.setMapName(newMapName);
	info.setMapHeader(header);
	info.setAuthorName(authorName);
	info.setDimensions(w, h);
	info.setSize(getCompressedSize(nmapFile));
	std::shared_ptr<NetRequestMapUpload> message(new NetRequestMapUpload(info));
	client->sendNetMessage(message);
	state = WaitingForUploadReply;
}



void YOGClientMapUploader::cancelUpload()
{
	if(state == UploadingMap)
	{
		client->getYOGClientFileAssembler(fileID)->cancelSendingFile();
		state = Nothing;
	}
}



void YOGClientMapUploader::recieveMessage(std::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	//This recieves the server information
	if(type==MNetAcceptMapUpload)
	{
		shared_ptr<NetAcceptMapUpload> info = static_pointer_cast<NetAcceptMapUpload>(message);
		fileID = info->getFileID();
		state = UploadingMap;
		
		std::shared_ptr<YOGClientFileAssembler> assembler(new YOGClientFileAssembler(client, fileID));
		assembler->startSendingFile(mapFile);
		client->setYOGClientFileAssembler(fileID, assembler);
	}
	else if(type==MNetRefuseMapUpload)
	{
		shared_ptr<NetRefuseMapUpload> info = static_pointer_cast<NetRefuseMapUpload>(message);
		reason = info->getReason();
		state = Nothing;
	}
}



void YOGClientMapUploader::update()
{
	if(state == UploadingMap && getPercentUploaded()==100)
	{
		state = Finished;
	}
}



YOGClientMapUploader::UploadingState YOGClientMapUploader::getUploadingState()
{
	return state;
}



YOGMapUploadRefusalReason YOGClientMapUploader::getRefusalReason()
{
	return reason;
}



int YOGClientMapUploader::getPercentUploaded()
{
	return client->getYOGClientFileAssembler(fileID)->getPercentage();
}



int YOGClientMapUploader::getCompressedSize(const std::string& mapname)
{
	Toolkit::getFileManager()->gzip(mapname, mapname+".gz");
	BinaryInputStream* istream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapname+".gz"));
	istream->seekFromEnd(0);
	int size=istream->getPosition();
	istream->seekFromStart(0);
	delete istream;
	return size;
}

