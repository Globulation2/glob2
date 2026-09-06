// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "FileTransferMessages.h"
#include <algorithm>
#include <iostream>
#include <sstream>

using namespace GAGCore;

NetRequestFile::NetRequestFile()
	: fileID(0)
{

}

NetRequestFile::NetRequestFile(Uint16 fileID)
	: fileID(fileID)
{

}

Uint8 NetRequestFile::getMessageType() const
{
	return MNetRequestFile;
}

void NetRequestFile::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetRequestFile");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}

void NetRequestFile::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetRequestFile");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}

std::string NetRequestFile::format() const
{
	std::ostringstream s;
	s<<"NetRequestFile(fileID="<<fileID<<")";
	return s.str();
}

bool NetRequestFile::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetRequestFile))
	{
		const NetRequestFile& r = dynamic_cast<const NetRequestFile&>(rhs);
		if(fileID == r.fileID)
			return true;
	}
	return false;
}

Uint16 NetRequestFile::getFileID()
{
	return fileID;
}

NetSendFileInformation::NetSendFileInformation()
	: size(0), fileID(0)
{

}

NetSendFileInformation::NetSendFileInformation(Uint32 filesize, Uint16 fileID)
	: size(filesize), fileID(fileID)
{
}

Uint8 NetSendFileInformation::getMessageType() const
{
	return MNetSendFileInformation;
}

void NetSendFileInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendFileInformation");
	stream->writeUint32(size, "size");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}

void NetSendFileInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendFileInformation");
	size = stream->readUint32("size");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}

std::string NetSendFileInformation::format() const
{
	std::ostringstream s;
	s<<"NetSendFileInformation(size="<<size<<",fileID="<<fileID<<")";
	return s.str();
}

bool NetSendFileInformation::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendFileInformation))
	{
		const NetSendFileInformation& r = dynamic_cast<const NetSendFileInformation&>(rhs);
		if(r.size == size && r.fileID == fileID)
			return true;
	}
	return false;
}

Uint32 NetSendFileInformation::getFileSize() const
{
	return size;
}

Uint16 NetSendFileInformation::getFileID() const
{
	return fileID;
}

NetSendFileChunk::NetSendFileChunk()
{
	std::fill(data, data+4096, 0);
	size=0;
	fileID=0;
}

NetSendFileChunk::NetSendFileChunk(std::shared_ptr<GAGCore::InputStream> stream, Uint16 fileID)
	: fileID(fileID)
{
	size=0;
	int pos=0;
	while(!stream->isEndOfStream() && size < 4096)
	{
		stream->read(data+pos, 1, "");
		//For some reason the last byte is an overread, so it should be ignored
		if(!stream->isEndOfStream())
		{
			pos+=1;
			size+=1;
		}
	}
}

Uint8 NetSendFileChunk::getMessageType() const
{
	return MNetSendFileChunk;
}

void NetSendFileChunk::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetSendFileChunk");
	stream->writeUint32(size, "size");
	stream->write(data, size, "data");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}

void NetSendFileChunk::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetSendFileChunk");
	size = stream->readUint32("size");
	stream->read(data, size, "data");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}

std::string NetSendFileChunk::format() const
{
	std::ostringstream s;
	s<<"NetSendFileChunk(size="<<size<<",fileID="<<fileID<<")";
	return s.str();
}

bool NetSendFileChunk::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetSendFileChunk))
	{
		const NetSendFileChunk& r = dynamic_cast<const NetSendFileChunk&>(rhs);
		for(int i=0; i<4096; ++i)
		{
			if(data[i] != r.data[i])
				return false;
		}
		if(fileID != r.fileID)
			return false;
		return true;
	}
	return false;
}

const Uint8* NetSendFileChunk::getBuffer() const
{
	return data;
}

Uint32 NetSendFileChunk::getChunkSize() const
{
	return size;
}

Uint16 NetSendFileChunk::getFileID() const
{
	return fileID;
}

NetCancelSendingFile::NetCancelSendingFile()
	: fileID(0)
{

}

NetCancelSendingFile::NetCancelSendingFile(Uint16 fileID)
	:fileID(fileID)
{
}

Uint8 NetCancelSendingFile::getMessageType() const
{
	return MNetCancelSendingFile;
}

void NetCancelSendingFile::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCancelSendingFile");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}

void NetCancelSendingFile::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCancelSendingFile");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}

std::string NetCancelSendingFile::format() const
{
	std::ostringstream s;
	s<<"NetCancelSendingFile("<<"fileID="<<fileID<<"; "<<")";
	return s.str();
}

bool NetCancelSendingFile::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCancelSendingFile))
	{
		const NetCancelSendingFile& r = dynamic_cast<const NetCancelSendingFile&>(rhs);
		if(r.fileID == fileID)
			return true;
	}
	return false;
}

Uint16 NetCancelSendingFile::getFileID() const
{
	return fileID;
}

NetCancelRecievingFile::NetCancelRecievingFile()
	: fileID(0)
{

}

NetCancelRecievingFile::NetCancelRecievingFile(Uint16 fileID)
	:fileID(fileID)
{
}

Uint8 NetCancelRecievingFile::getMessageType() const
{
	return MNetCancelRecievingFile;
}

void NetCancelRecievingFile::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("NetCancelRecievingFile");
	stream->writeUint16(fileID, "fileID");
	stream->writeLeaveSection();
}

void NetCancelRecievingFile::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("NetCancelRecievingFile");
	fileID = stream->readUint16("fileID");
	stream->readLeaveSection();
}

std::string NetCancelRecievingFile::format() const
{
	std::ostringstream s;
	s<<"NetCancelRecievingFile("<<"fileID="<<fileID<<"; "<<")";
	return s.str();
}

bool NetCancelRecievingFile::operator==(const NetMessage& rhs) const
{
	if(typeid(rhs)==typeid(NetCancelRecievingFile))
	{
		const NetCancelRecievingFile& r = dynamic_cast<const NetCancelRecievingFile&>(rhs);
		if(r.fileID == fileID)
			return true;
	}
	return false;
}

Uint16 NetCancelRecievingFile::getFileID() const
{
	return fileID;
}
