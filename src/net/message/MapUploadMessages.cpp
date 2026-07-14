// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "MapUploadMessages.h"
#include <iostream>
#include <sstream>
#include "Version.h"

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
