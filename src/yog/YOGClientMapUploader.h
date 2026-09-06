// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <memory>
#include "YOGConsts.h"
#include <string>

class YOGClient;
class NetMessage;

///This class manages the uploading of a map to the server
class YOGClientMapUploader
{
public:
	///Constructs a map uploader
	YOGClientMapUploader(std::shared_ptr<YOGClient> client);
	
	///Removes the map uploader
	~YOGClientMapUploader();

	///Starts uploading the given map, with the given name, with the given author name
	void startUploading(const std::string& mapFile, const std::string& newMapName, const std::string& authorName, int w, int h);
	
	///If this uploader is uploading a map, this will cancel the upload
	void cancelUpload();
	
	///This recieves a message from the server
	void recieveMessage(std::shared_ptr<NetMessage> message);
	
	///This updates the uploader
	void update();

	enum UploadingState
	{
		Nothing,
		WaitingForUploadReply,
		UploadingMap,
		Finished,
	};
	///Returns the current uploading state
	UploadingState getUploadingState();
	
	///Returns the current refusal reason
	YOGMapUploadRefusalReason getRefusalReason();
	
	///Returns the percent uploaded
	int getPercentUploaded();
	
	///Returns the size of a file compressed
	int getCompressedSize(const std::string& mapName);
private:
	UploadingState state;
	std::shared_ptr<YOGClient> client;
	Uint16 fileID;
	YOGMapUploadRefusalReason reason;
	std::string mapFile;
};

