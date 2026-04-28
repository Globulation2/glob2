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

NetRequestMapUpload::NetRequestMapUpload()
	: mapInfo()
{

}



NetRequestMapUpload::NetRequestMapUpload(YOGDownloadableMapInfo mapInfo)
	:mapInfo(mapInfo)
{
}



Uint8 NetRequestMapUpload::getMessageType() const
{
	return MNetRequestMapUpload;
}



void NetRequestMapUpload::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestMapUpload");
	mapInfo.encodeData(stream);
	stream->writeLeaveSection();
}



void NetRequestMapUpload::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestMapUpload");
	mapInfo.decodeData(stream, VERSION_MINOR);
	stream->readLeaveSection();
}



std::string NetRequestMapUpload::format() const
{
	std::ostringstream s;
	s<<"NetRequestMapUpload("<<"""="<<""<<"; "<<")";
	return s.str();
}



bool NetRequestMapUpload::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestMapUpload))
	{
		const NetRequestMapUpload& r = dynamic_cast<const NetRequestMapUpload&>(rhs);
		if(r.mapInfo == mapInfo)
			return true;
	}
	return false;
}


YOGDownloadableMapInfo NetRequestMapUpload::getMapInfo() const
{
	return mapInfo;
}




NetAcceptMapUpload::NetAcceptMapUpload()
	: fileID(0)
{

}



NetAcceptMapUpload::NetAcceptMapUpload(Uint16 fileID)
	:fileID(fileID)
{
}



Uint8 NetAcceptMapUpload::getMessageType() const
{
	return MNetAcceptMapUpload;
}



void NetAcceptMapUpload::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetAcceptMapUpload");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}



void NetAcceptMapUpload::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetAcceptMapUpload");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}



std::string NetAcceptMapUpload::format() const
{
	std::ostringstream s;
	s<<"NetAcceptMapUpload("<<"fileID="<<fileID<<"; "<<")";
	return s.str();
}



bool NetAcceptMapUpload::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetAcceptMapUpload))
	{
		const NetAcceptMapUpload& r = dynamic_cast<const NetAcceptMapUpload&>(rhs);
		if(r.fileID == fileID)
			return true;
	}
	return false;
}


Uint16 NetAcceptMapUpload::getFileID() const
{
	return fileID;
}




NetRefuseMapUpload::NetRefuseMapUpload()
	: reason(YOGMapUploadReasonUnknown)
{

}



NetRefuseMapUpload::NetRefuseMapUpload(YOGMapUploadRefusalReason reason)
	:reason(reason)
{
}



Uint8 NetRefuseMapUpload::getMessageType() const
{
	return MNetRefuseMapUpload;
}



void NetRefuseMapUpload::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRefuseMapUpload");
	stream->writeUint8(static_cast<Uint8>(reason), "reason");
	stream->writeLeaveSection();
}



void NetRefuseMapUpload::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRefuseMapUpload");
	reason = static_cast<YOGMapUploadRefusalReason>(stream->readUint8("reason"));
	stream->readLeaveSection();
}



std::string NetRefuseMapUpload::format() const
{
	std::ostringstream s;
	s<<"NetRefuseMapUpload("<<"reason="<<reason<<"; "<<")";
	return s.str();
}



bool NetRefuseMapUpload::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRefuseMapUpload))
	{
		const NetRefuseMapUpload& r = dynamic_cast<const NetRefuseMapUpload&>(rhs);
		if(r.reason == reason)
			return true;
	}
	return false;
}


YOGMapUploadRefusalReason NetRefuseMapUpload::getReason() const
{
	return reason;
}
