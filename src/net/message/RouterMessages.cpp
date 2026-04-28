/*
  Copyright (C) 2007 Bradley Arsenault

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

#include "NetMessage.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include "Version.h"
#include "BinaryStream.h"

using namespace GAGCore;

NetRegisterRouter::NetRegisterRouter()
{

}



Uint8 NetRegisterRouter::getMessageType() const
{
	return MNetRegisterRouter;
}



void NetRegisterRouter::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRegisterRouter");
	stream->writeLeaveSection();
}



void NetRegisterRouter::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRegisterRouter");
	stream->readLeaveSection();
}



std::string NetRegisterRouter::format() const
{
	std::ostringstream s;
	s<<"NetRegisterRouter()";
	return s.str();
}



bool NetRegisterRouter::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRegisterRouter))
	{
		//const NetRegisterRouter& r = dynamic_cast<const NetRegisterRouter&>(rhs);
		return true;
	}
	return false;
}



NetAcknowledgeRouter::NetAcknowledgeRouter()
{

}



Uint8 NetAcknowledgeRouter::getMessageType() const
{
	return MNetAcknowledgeRouter;
}



void NetAcknowledgeRouter::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAcknowledgeRouter");
	stream->writeLeaveSection();
}



void NetAcknowledgeRouter::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAcknowledgeRouter");
	stream->readLeaveSection();
}



std::string NetAcknowledgeRouter::format() const
{
	std::ostringstream s;
	s<<"NetAcknowledgeRouter()";
	return s.str();
}



bool NetAcknowledgeRouter::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAcknowledgeRouter))
	{
		//const NetAcknowledgeRouter& r = dynamic_cast<const NetAcknowledgeRouter&>(rhs);
		return true;
	}
	return false;
}



NetSetGameInRouter::NetSetGameInRouter()
	: gameID(0)
{

}



NetSetGameInRouter::NetSetGameInRouter(Uint16 gameID)
	:gameID(gameID)
{
}



Uint8 NetSetGameInRouter::getMessageType() const
{
	return MNetSetGameInRouter;
}



void NetSetGameInRouter::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSetGameInRouter");
	stream->writeUint16(gameID, "gameID");
	stream->writeLeaveSection();
}



void NetSetGameInRouter::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSetGameInRouter");
	gameID = stream->readUint16("gameID");
	stream->readLeaveSection();
}



std::string NetSetGameInRouter::format() const
{
	std::ostringstream s;
	s<<"NetSetGameInRouter("<<"gameID="<<gameID<<"; "<<")";
	return s.str();
}



bool NetSetGameInRouter::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSetGameInRouter))
	{
		const NetSetGameInRouter& r = dynamic_cast<const NetSetGameInRouter&>(rhs);
		if(r.gameID == gameID)
			return true;
	}
	return false;
}


Uint16 NetSetGameInRouter::getGameID() const
{
	return gameID;
}
