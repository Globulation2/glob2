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

#ifndef YOGClientMapUploader_h
#define YOGClientMapUploader_h

#include "boost/shared_ptr.hpp"
#include "YOGConsts.h"
#include <string>

class YOGClient;
class NetMessage;

///This class manages the uploading of a map to the server
class YOGClientMapUploader
{
public:
	///Constructs a map uploader
	YOGClientMapUploader(boost::shared_ptr<YOGClient> client);
	
	///Removes the map uploader
	~YOGClientMapUploader();

	///Starts uploading the given map, with the given name, with the given author name
	void startUploading(const std::string& mapFile, const std::string& newMapName, const std::string& authorName);
	
	///If this uploader is uploading a map, this will cancel the upload
	void cancelUpload();
	
	///This recieves a message from the server
	void recieveMessage(boost::shared_ptr<NetMessage> message);
	
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
private:
	UploadingState state;
	boost::shared_ptr<YOGClient> client;
	Uint16 fileID;
	YOGMapUploadRefusalReason reason;
	std::string mapFile;
};

#endif
