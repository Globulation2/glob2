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


