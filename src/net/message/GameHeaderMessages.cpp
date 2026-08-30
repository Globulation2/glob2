// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameHeaderMessages.h"
#include <sstream>
#include "Version.h"
#include "BinaryStream.h"

using namespace GAGCore;

NetSendMapHeader::NetSendMapHeader()
{

}

NetSendMapHeader::NetSendMapHeader(const MapHeader& mapHeader)
	: mapHeader(mapHeader)
{

}

Uint8 NetSendMapHeader::getMessageType() const
{
	return MNetSendMapHeader;
}

void NetSendMapHeader::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendMapHeader");
	mapHeader.save(stream);
	stream->writeLeaveSection();
}

void NetSendMapHeader::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendMapHeader");
	mapHeader.load(stream);
	stream->readLeaveSection();
}

std::string NetSendMapHeader::format() const
{
	std::ostringstream s;
	s<<"NetSendMapHeader(mapname="+mapHeader.getMapName()+")";
	return s.str();
}

bool NetSendMapHeader::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendMapHeader))
	{
		//const NetSendMapHeader& r = dynamic_cast<const NetSendMapHeader&>(rhs);
		return true;
	}
	return false;
}

const MapHeader& NetSendMapHeader::getMapHeader() const
{
	return mapHeader;
}

NetSendGameHeader::NetSendGameHeader()
{

}

NetSendGameHeader::NetSendGameHeader(const GameHeader& gameHeader)
	:	gameHeader(gameHeader)
{

}

Uint8 NetSendGameHeader::getMessageType() const
{
	return MNetSendGameHeader;
}

void NetSendGameHeader::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendGameHeader");
	gameHeader.saveWithoutPlayerInfo(stream);
	stream->writeLeaveSection();
}

void NetSendGameHeader::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendGameHeader");
	gameHeader.loadWithoutPlayerInfo(stream, VERSION_MINOR);
	stream->readLeaveSection();
}

std::string NetSendGameHeader::format() const
{
	std::ostringstream s;
	s<<"NetSendGameHeader()";
	return s.str();
}

bool NetSendGameHeader::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendGameHeader))
	{
		//const NetSendGameHeader& r = dynamic_cast<const NetSendGameHeader&>(rhs);
//		if(gameHeader == r.gameHeader)
		return true;
	}
	return false;
}

void NetSendGameHeader::downloadToGameHeader(GameHeader& newGameHeader)
{
	//This is a special trick used to avoid having to manually copy over every
	//variable
	MemoryStreamBackend* obackend = new MemoryStreamBackend;
	GAGCore::BinaryOutputStream* ostream = new BinaryOutputStream(obackend);
	gameHeader.saveWithoutPlayerInfo(ostream);

	obackend->seekFromStart(0);
	MemoryStreamBackend* ibackend = new MemoryStreamBackend(*obackend);
	GAGCore::BinaryInputStream* istream = new BinaryInputStream(ibackend);
	newGameHeader.loadWithoutPlayerInfo(istream, VERSION_MINOR);

	delete ostream;
	delete istream;
}

NetSendGamePlayerInfo::NetSendGamePlayerInfo()
{

}

NetSendGamePlayerInfo::NetSendGamePlayerInfo(GameHeader& gameHeader)
	:	gameHeader(gameHeader)
{
}

Uint8 NetSendGamePlayerInfo::getMessageType() const
{
	return MNetSendGamePlayerInfo;
}

void NetSendGamePlayerInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendGamePlayerInfo");
	gameHeader.savePlayerInfo(stream);
	stream->writeLeaveSection();
}

void NetSendGamePlayerInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendGamePlayerInfo");
	gameHeader.loadPlayerInfo(stream, VERSION_MINOR);
	stream->readLeaveSection();
}

std::string NetSendGamePlayerInfo::format() const
{
	std::ostringstream s;
	s<<"NetSendGamePlayerInfo()";
	return s.str();
}

bool NetSendGamePlayerInfo::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendGamePlayerInfo))
	{
		//const NetSendGamePlayerInfo& r = dynamic_cast<const NetSendGamePlayerInfo&>(rhs);
		return true;
	}
	return false;
}

void NetSendGamePlayerInfo::downloadToGameHeader(GameHeader& header)
{
	//This is a special trick used to avoid having to manually copy over every
	//variable
	MemoryStreamBackend* obackend = new MemoryStreamBackend;
	GAGCore::BinaryOutputStream* ostream = new BinaryOutputStream(obackend);
	gameHeader.savePlayerInfo(ostream);

	obackend->seekFromStart(0);
	MemoryStreamBackend* ibackend = new MemoryStreamBackend(*obackend);
	GAGCore::BinaryInputStream* istream = new BinaryInputStream(ibackend);
	header.loadPlayerInfo(istream, VERSION_MINOR);

	delete ostream;
	delete istream;
}
