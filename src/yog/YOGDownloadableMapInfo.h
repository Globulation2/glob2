// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "MapHeader.h"
#include <string>
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This is all of the information for a map that can be downloaded
class YOGDownloadableMapInfo
{
public:
	///Constructs a blank info
	YOGDownloadableMapInfo();
	
	///Constructs the map info
	YOGDownloadableMapInfo(MapHeader& header);
	
	///Sets the map header
	void setMapHeader(const MapHeader& header);
	
	///Returns the map header
	const MapHeader& getMapHeader() const;
	
	///This sets the rating total.
	void setRatingTotal(Uint32 total);
	
	///This returns the rating total.
	Uint32 getRatingTotal() const;
	
	///This sets the number of ratings
	void setNumberOfRatings(Uint32 numberOfRatings);
	
	///This returns the number of ratings
	Uint32 getNumberOfRatings() const;
	
	///This sets the author name
	void setAuthorName(const std::string& authorname);
	
	///This returns the author name
	std::string getAuthorName() const;
	
	///This is the fileID this downloadable map can be obtained from
	Uint16 getFileID() const;
	
	///This sets the fileID that this downloadable map is obtained from
	void setFileID(Uint16 fileID);
	
	///This is the mapID of this downloadable map
	Uint16 getMapID() const;
	
	///This sets the mapID that this downloadable map is obtained from
	void setMapID(Uint16 mapID);
	
	///This returns the width of the map
	Uint16 getWidth() const;
	
	///This returns the height of the map
	Uint16 getHeight() const;
	
	///This sets the dimensions of the map
	void setDimensions(Uint16 width, Uint16 height);
	
	///This returns the size of the map compressed in bytes
	void setSize(int bytes);
	
	///This returns the size of the map compressed in bytes
	int getSize() const;

	///Encodes this YOGGameInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///Test for equality between two YOGDownloadableMapInfo
	bool operator==(const YOGDownloadableMapInfo& rhs) const;
	bool operator!=(const YOGDownloadableMapInfo& rhs) const;
private:
	MapHeader mapHeader;
	Uint32 total;
	Uint32 numberOfRatings;
	std::string author;
	Uint16 fileID;
	Uint16 mapID;
	Uint16 width;
	Uint16 height;
	Uint32 size;
};

