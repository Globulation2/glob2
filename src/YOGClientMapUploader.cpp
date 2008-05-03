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

#include "Engine.h"
#include "NetMessage.h"
#include "YOGClientFileAssembler.h"
#include "YOGClient.h"
#include "YOGClientMapUploader.h"
#include "YOGConsts.h"


YOGClientMapUploader::YOGClientMapUploader(boost::shared_ptr<YOGClient> client)
	: client(client), state(Nothing)
{
	client->setMapUploader(this);
	fileID=0;
	reason=YOGMapUploadReasonUnknown;
}



YOGClientMapUploader::~YOGClientMapUploader()
{
	client->setMapUploader(NULL);
}



void YOGClientMapUploader::startUploading(const std::string& nmapFile, const std::string& newMapName, const std::string& authorName)
{
	mapFile = nmapFile;
	Engine engine;
	YOGDownloadableMapInfo info;
	MapHeader header = engine.loadMapHeader(mapFile);
	header.setMapName(newMapName);
	info.setMapHeader(header);
	info.setAuthorName(authorName);
	boost::shared_ptr<NetRequestMapUpload> message(new NetRequestMapUpload(info));
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



void YOGClientMapUploader::recieveMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	//This recieves the server information
	if(type==MNetAcceptMapUpload)
	{
		shared_ptr<NetAcceptMapUpload> info = static_pointer_cast<NetAcceptMapUpload>(message);
		fileID = info->getFileID();
		state = UploadingMap;
		
		boost::shared_ptr<YOGClientFileAssembler> assembler(new YOGClientFileAssembler(client, fileID));
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


