// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "MapDatabaseMessages.h"
#include <iostream>
#include <sstream>
#include "Version.h"

using namespace GAGCore;

NetDownloadableMapInfos::NetDownloadableMapInfos()
	: maps()
{

}



NetDownloadableMapInfos::NetDownloadableMapInfos(std::vector<YOGDownloadableMapInfo> maps)
	:maps(maps)
{
}



Uint8 NetDownloadableMapInfos::getMessageType() const
{
	return MNetDownloadableMapInfos;
}



void NetDownloadableMapInfos::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetDownloadableMapInfos");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "size");
	for(unsigned int i=0; i<maps.size(); ++i)
	{
		stream->writeEnterSection(i);
		maps[i].encodeData(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



void NetDownloadableMapInfos::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetDownloadableMapInfos");
	stream->readEnterSection("maps");
	Uint32 size = stream->readUint32("maps");
	maps.resize(size);
	for(unsigned int i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		maps[i].decodeData(stream, VERSION_MINOR);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}



std::string NetDownloadableMapInfos::format() const
{
	std::ostringstream s;
	s<<"NetDownloadableMapInfos(maps.size()="<<maps.size()<<"; "<<")";
	return s.str();
}



bool NetDownloadableMapInfos::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetDownloadableMapInfos))
	{
		const NetDownloadableMapInfos& r = dynamic_cast<const NetDownloadableMapInfos&>(rhs);
		if(r.maps == maps)
			return true;
	}
	return false;
}


std::vector<YOGDownloadableMapInfo> NetDownloadableMapInfos::getMaps() const
{
	return maps;
}




NetRequestDownloadableMapList::NetRequestDownloadableMapList()
{

}



Uint8 NetRequestDownloadableMapList::getMessageType() const
{
	return MNetRequestDownloadableMapList;
}



void NetRequestDownloadableMapList::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestDownloadableMapList");
	stream->writeLeaveSection();
}



void NetRequestDownloadableMapList::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestDownloadableMapList");
	stream->readLeaveSection();
}



std::string NetRequestDownloadableMapList::format() const
{
	std::ostringstream s;
	s<<"NetRequestDownloadableMapList()";
	return s.str();
}



bool NetRequestDownloadableMapList::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestDownloadableMapList))
	{
		//const NetRequestDownloadableMapList& r = dynamic_cast<const NetRequestDownloadableMapList&>(rhs);
		return true;
	}
	return false;
}



NetRequestMapThumbnail::NetRequestMapThumbnail()
	: mapID(0)
{

}



NetRequestMapThumbnail::NetRequestMapThumbnail(Uint16 mapID)
	: mapID(mapID)
{
}



Uint8 NetRequestMapThumbnail::getMessageType() const
{
	return MNetRequestMapThumbnail;
}



void NetRequestMapThumbnail::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestMapThumbnail");
	stream->writeUint16(mapID, "mapID");
	stream->writeLeaveSection();
}



void NetRequestMapThumbnail::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestMapThumbnail");
	mapID = stream->readUint16("mapID");
	stream->readLeaveSection();
}



std::string NetRequestMapThumbnail::format() const
{
	std::ostringstream s;
	s<<"NetRequestMapThumbnail("<<"mapID="<<mapID<<"; "<<")";
	return s.str();
}



bool NetRequestMapThumbnail::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestMapThumbnail))
	{
		const NetRequestMapThumbnail& r = dynamic_cast<const NetRequestMapThumbnail&>(rhs);
		if(r.mapID == mapID)
			return true;
	}
	return false;
}


Uint16 NetRequestMapThumbnail::getMapID() const
{
	return mapID;
}




NetSendMapThumbnail::NetSendMapThumbnail()
	: mapID(0), thumbnail()
{

}



NetSendMapThumbnail::NetSendMapThumbnail(Uint16 mapID, MapThumbnail thumbnail)
	:mapID(mapID), thumbnail(thumbnail)
{
}



Uint8 NetSendMapThumbnail::getMessageType() const
{
	return MNetSendMapThumbnail;
}



void NetSendMapThumbnail::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendMapThumbnail");
	stream->writeUint16(mapID, "mapID");
	thumbnail.encodeData(stream);
	stream->writeLeaveSection();
}



void NetSendMapThumbnail::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendMapThumbnail");
	mapID = stream->readUint16("mapID");
	thumbnail.decodeData(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetSendMapThumbnail::format() const
{
	std::ostringstream s;
	s<<"NetSendMapThumbnail("<<"mapID="<<mapID<<"; "<<"="<<""<<"; "<<")";
	return s.str();
}



bool NetSendMapThumbnail::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendMapThumbnail))
	{
		const NetSendMapThumbnail& r = dynamic_cast<const NetSendMapThumbnail&>(rhs);
		if(r.mapID == mapID)
			return true;
	}
	return false;
}


Uint16 NetSendMapThumbnail::getMapID() const
{
	return mapID;
}



MapThumbnail NetSendMapThumbnail::getThumbnail() const
{
	return thumbnail;
}




NetSubmitRatingOnMap::NetSubmitRatingOnMap()
	: mapID(0), rating(0)
{

}



NetSubmitRatingOnMap::NetSubmitRatingOnMap(Uint16 mapID, Uint8 rating)
	:mapID(mapID), rating(rating)
{
}



Uint8 NetSubmitRatingOnMap::getMessageType() const
{
	return MNetSubmitRatingOnMap;
}



void NetSubmitRatingOnMap::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSubmitRatingOnMap");
	stream->writeUint16(mapID, "mapID");
	stream->writeUint8(rating, "rating");
	stream->writeLeaveSection();
}



void NetSubmitRatingOnMap::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSubmitRatingOnMap");
	mapID = stream->readUint16("mapID");
	rating = stream->readUint8("rating");
	stream->readLeaveSection();
}



std::string NetSubmitRatingOnMap::format() const
{
	std::ostringstream s;
	s<<"NetSubmitRatingOnMap("<<"mapID="<<mapID<<"; "<<"rating="<<rating<<"; "<<")";
	return s.str();
}



bool NetSubmitRatingOnMap::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSubmitRatingOnMap))
	{
		const NetSubmitRatingOnMap& r = dynamic_cast<const NetSubmitRatingOnMap&>(rhs);
		if(r.mapID == mapID && r.rating == rating)
			return true;
	}
	return false;
}


Uint16 NetSubmitRatingOnMap::getMapID() const
{
	return mapID;
}



Uint8 NetSubmitRatingOnMap::getRating() const
{
	return rating;
}
