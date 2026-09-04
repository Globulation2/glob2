// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGAfterJoinGameInformation.h"
#include "Version.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

YOGAfterJoinGameInformation::YOGAfterJoinGameInformation()
{

}



void YOGAfterJoinGameInformation::setMapHeader(const MapHeader& header)
{
	map = header;
}



const MapHeader& YOGAfterJoinGameInformation::getMapHeader() const
{
	return map;
}



void YOGAfterJoinGameInformation::setGameHeader(const GameHeader& header)
{
	game = header;
}



const GameHeader& YOGAfterJoinGameInformation::getGameHeader() const
{
	return game;
}



void YOGAfterJoinGameInformation::setReteamingInformation(const NetReteamingInformation& nreteam)
{
	reteam = nreteam;
}



const NetReteamingInformation& YOGAfterJoinGameInformation::getReteamingInformation() const
{
	return reteam;
}



void YOGAfterJoinGameInformation::setLatencyAdjustment(Uint8 nlatency)
{
	latency = nlatency;
}



Uint8 YOGAfterJoinGameInformation::getLatencyAdjustment() const
{
	return latency;
}



void YOGAfterJoinGameInformation::setGameRouterIP(const std::string& ip)
{
	routerIP = ip;
}



const std::string& YOGAfterJoinGameInformation::getGameRouterIP() const
{
	return routerIP;
}



void YOGAfterJoinGameInformation::setMapFileID(Uint16 nfileID)
{
	fileID = nfileID;
}
	


Uint16 YOGAfterJoinGameInformation::getMapFileID() const
{
	return fileID;
}



void YOGAfterJoinGameInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGAfterJoinGameInformation");
	map.save(stream);
	game.save(stream);
	reteam.encodeData(stream);
	stream->writeUint8(latency, "latency");
	stream->writeText(routerIP, "routerIP");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void YOGAfterJoinGameInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGAfterJoinGameInformation");
	map.load(stream);
	game.load(stream, VERSION_MINOR);
	reteam.decodeData(stream);
	latency = stream->readUint8("latency");
	routerIP = stream->readText("routerIP");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



bool YOGAfterJoinGameInformation::operator==(const YOGAfterJoinGameInformation& rhs) const
{
	if(map == rhs.map && reteam == rhs.reteam && latency == rhs.latency && routerIP == rhs.routerIP)
		return true;
	return false;
}



bool YOGAfterJoinGameInformation::operator!=(const YOGAfterJoinGameInformation& rhs) const
{
	if(map != rhs.map || reteam != rhs.reteam || latency != rhs.latency || routerIP != rhs.routerIP)
		return true;
	return false;
}


