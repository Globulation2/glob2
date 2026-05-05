// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGDownloadableMapInfo.h"
#include "Stream.h"


YOGDownloadableMapInfo::YOGDownloadableMapInfo()
{
	total = 0;
	numberOfRatings = 0;
	fileID = 0;
	mapID = 0;
	width = 0;
	height = 0;
	size = 0;
}



YOGDownloadableMapInfo::YOGDownloadableMapInfo(MapHeader& header)
	: mapHeader(header)
{
	total = 0;
	numberOfRatings = 0;
	fileID = 0;
	mapID = 0;
	width = 0;
	height = 0;
	size = 0;
}



void YOGDownloadableMapInfo::setMapHeader(const MapHeader& header)
{
	mapHeader = header;
}



const MapHeader& YOGDownloadableMapInfo::getMapHeader() const
{
	return mapHeader;
}



void YOGDownloadableMapInfo::setRatingTotal(Uint32 ntotal)
{
	total=ntotal;
}



Uint32 YOGDownloadableMapInfo::getRatingTotal() const
{
	return total;
}



void YOGDownloadableMapInfo::setNumberOfRatings(Uint32 nnumberOfRatings)
{
	numberOfRatings = nnumberOfRatings;
}



Uint32 YOGDownloadableMapInfo::getNumberOfRatings() const
{
	return numberOfRatings;
}



void YOGDownloadableMapInfo::setAuthorName(const std::string& authorname)
{
	author = authorname;
}



std::string YOGDownloadableMapInfo::getAuthorName() const
{
	return author;
}



Uint16 YOGDownloadableMapInfo::getFileID() const
{
	return fileID;
}



void YOGDownloadableMapInfo::setFileID(Uint16 nfileID)
{
	fileID = nfileID;
}



Uint16 YOGDownloadableMapInfo::getMapID() const
{
	return mapID;
}
	


void YOGDownloadableMapInfo::setMapID(Uint16 nmapID)
{
	mapID = nmapID;
}



Uint16 YOGDownloadableMapInfo::getWidth() const
{
	return width;
}



Uint16 YOGDownloadableMapInfo::getHeight() const
{
	return height;
}



void YOGDownloadableMapInfo::setDimensions(Uint16 nwidth, Uint16 nheight)
{
	width = nwidth;
	height = nheight;
}



void YOGDownloadableMapInfo::setSize(int bytes)
{
	size = bytes;
}



int YOGDownloadableMapInfo::getSize() const
{
	return size;
}



void YOGDownloadableMapInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGDownloadableMapInfo");
	mapHeader.save(stream);
	stream->writeUint32(total, "total");
	stream->writeUint32(numberOfRatings, "numberOfRatings");
	stream->writeText(author, "author");
	stream->writeUint16(fileID, "fileID");
	stream->writeUint16(mapID, "mapID");
	stream->writeUint16(width, "width");
	stream->writeUint16(height, "height");
	stream->writeUint32(size, "size");
	stream->writeLeaveSection();
}



void YOGDownloadableMapInfo::decodeData(GAGCore::InputStream* stream, Uint32 versionMinor)
{
	stream->readEnterSection("YOGDownloadableMapInfo");
	mapHeader.load(stream);
	total = stream->readUint32("total");
	numberOfRatings = stream->readUint32("numberOfRatings");
	author = stream->readText("author");
	fileID = stream->readUint16("fileID");
	mapID = stream->readUint16("mapID");
	width = stream->readUint16("width");
	height = stream->readUint16("height");
	size = stream->readUint32("size");
	stream->readLeaveSection();
}



bool YOGDownloadableMapInfo::operator==(const YOGDownloadableMapInfo& rhs) const
{
	if(mapHeader == rhs.mapHeader && total == rhs.total && numberOfRatings == rhs.numberOfRatings && author == rhs.author && fileID == rhs.fileID && mapID == rhs.mapID && rhs.width == width && rhs.height == height && rhs.size == size)
		return true;
	return false;
}


bool YOGDownloadableMapInfo::operator!=(const YOGDownloadableMapInfo& rhs) const
{
	if(mapHeader != rhs.mapHeader || total != rhs.total || numberOfRatings != rhs.numberOfRatings || author != rhs.author || fileID != rhs.fileID || mapID != rhs.mapID || rhs.width != width || rhs.height != height || rhs.size != size)
		return true;
	return false;
}



