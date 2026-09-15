// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "BinaryStream.h"
#include "FileManager.h"
#include "FileTransferMessages.h"
#include "MapHeader.h"
#include "StreamBackend.h"
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
	FileManager& files = *Toolkit::getFileManager();
	const std::string resolved = glob2PreferGzipReadPath(files, mapname);
	// A locally compressed map is already the wire payload; only a legacy raw
	// map needs gzipping once before it is sent.
	std::string gzipFile = resolved;
	if (!glob2IsGzipPath(resolved))
	{
		files.gzip(resolved, resolved+".gz");
		gzipFile = resolved+".gz";
	}
	finished=0;
	istream.reset(new BinaryInputStream(files.openInputStreamBackend(gzipFile)));
	istream->seekFromEnd(0);
	size=istream->getPosition();
	istream->seekFromStart(0);
	shared_ptr<NetSendFileInformation> message(new NetSendFileInformation(size, fileID));
	nclient->sendNetMessage(message);
	mode=SendingFile;
}



void YOGClientFileAssembler::startReceivingFile(std::string mapname)
{
	filename=mapname;
	obackend = new MemoryStreamBackend;
	ostream.reset(new BinaryOutputStream(obackend));
	mode=ReceivingFile;
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
		if(mode == ReceivingFile)
		{
			shared_ptr<NetSendFileChunk> info = static_pointer_cast<NetSendFileChunk>(message);
			Uint32 bsize = info->getChunkSize();
			const Uint8* buffer = info->getBuffer();
			ostream->write(buffer, bsize, "");
			finished+=bsize;
			if(finished>=size)
			{
				mode=NoTransfer;
				// The wire payload is always a single gzip layer of the map bytes;
				// store it directly as the new on-disk container (atomically, so a
				// crash mid-write can't leave a corrupt file) instead of unzipping
				// to a raw file. An older receiver's gunzip-to-raw still works on
				// this same payload, so this is compatible with peers running that code.
				ostream->seekFromEnd(0);
				const size_t receivedSize = ostream->getPosition();
				const char* receivedData = obackend->getBuffer();
				Toolkit::getFileManager()->writeAtomically(filename+".gz", [&](OutputStream& stream) {
					stream.write(receivedData, receivedSize, "");
				});
				ostream.reset();
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



void YOGClientFileAssembler::cancelReceivingFile()
{
	std::shared_ptr<YOGClient> nclient(client);
	shared_ptr<NetCancelReceivingFile> message(new NetCancelReceivingFile(fileID));
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



bool YOGClientFileAssembler::fileInformationReceived()
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

