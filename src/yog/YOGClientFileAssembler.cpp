// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "BinaryStream.h"
#include "FileManager.h"
#include "NetMessage.h"
#include "StreamBackend.h"
#include "Stream.h"
#include "Toolkit.h"
#include "YOGClientFileAssembler.h"
#include "YOGClient.h"

using namespace GAGCore;
using std::static_pointer_cast;

YOGClientFileAssembler::YOGClientFileAssembler(std::weak_ptr<YOGClient> client, Uint16 fileID)
	: client(client), fileID(fileID)
{
	obackend = NULL;
	mode = NoTransfer;
	size = 0;
	finished=0;
	sendTime = boost::posix_time::second_clock::local_time();
}



void YOGClientFileAssembler::update()
{
	if(mode == SendingFile && finished < size)
	{
		boost::posix_time::ptime current = boost::posix_time::second_clock::local_time();
		if(sendTime < current)
		{
			sendNextChunk();
			sendTime = current+boost::posix_time::microseconds(100);
		}
	}
}



void YOGClientFileAssembler::startSendingFile(std::string mapname)
{
	std::shared_ptr<YOGClient> nclient(client);
	Toolkit::getFileManager()->gzip(mapname, mapname+".gz");
	finished=0;
	istream.reset(new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapname+".gz")));
	istream->seekFromEnd(0);
	size=istream->getPosition();
	istream->seekFromStart(0);
	shared_ptr<NetSendFileInformation> message(new NetSendFileInformation(size, fileID));
	nclient->sendNetMessage(message);
	mode=SendingFile;
}



void YOGClientFileAssembler::startRecievingFile(std::string mapname)
{
	filename=mapname;
	obackend = new MemoryStreamBackend;
	ostream.reset(new BinaryOutputStream(obackend));
	mode=RecivingFile;
	finished=0;
}



void YOGClientFileAssembler::handleMessage(std::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetSendFileInformation)
	{
		shared_ptr<NetSendFileInformation> info = static_pointer_cast<NetSendFileInformation>(message);
		size = info->getFileSize();
	}
	if(type == MNetSendFileChunk)
	{
		if(mode == RecivingFile)
		{
			shared_ptr<NetSendFileChunk> info = static_pointer_cast<NetSendFileChunk>(message);
			Uint32 bsize = info->getChunkSize();
			const Uint8* buffer = info->getBuffer();
			ostream->write(buffer, bsize, "");
			finished+=bsize;
			if(finished>=size)
			{
				mode=NoTransfer;
				//Write from the buffer, obackend, to the file
				BinaryOutputStream* fstream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename+".gz"));
				ostream->seekFromEnd(0);
				fstream->write(obackend->getBuffer(), ostream->getPosition(), "");
				delete fstream;
				ostream.reset();
				//unzip file
				Toolkit::getFileManager()->gunzip(filename+".gz", filename);
			}
		}
	}
}



void YOGClientFileAssembler::cancelSendingFile()
{
	std::shared_ptr<YOGClient> nclient(client);
	shared_ptr<NetCancelSendingFile> message(new NetCancelSendingFile(fileID));
	nclient->sendNetMessage(message);
	size = 0;
	finished = 0;
	mode = NoTransfer;
	ostream.reset();
	istream.reset();
}



void YOGClientFileAssembler::cancelRecievingFile()
{
	std::shared_ptr<YOGClient> nclient(client);
	shared_ptr<NetCancelRecievingFile> message(new NetCancelRecievingFile(fileID));
	nclient->sendNetMessage(message);
	size = 0;
	finished = 0;
	mode = NoTransfer;
	ostream.reset();
	istream.reset();
}



Uint8 YOGClientFileAssembler::getPercentage()
{
	if(size == 0)
		return 100;

	return (finished * 100) / size;
}



bool YOGClientFileAssembler::fileInformationRecieved()
{
	if(size == 0)
		return false;
	return true;
}



void YOGClientFileAssembler::sendNextChunk()
{
	std::shared_ptr<YOGClient> nclient(client);
	shared_ptr<NetSendFileChunk> message(new NetSendFileChunk(istream, fileID));
	finished += message->getChunkSize();
	nclient->sendNetMessage(message);
}

