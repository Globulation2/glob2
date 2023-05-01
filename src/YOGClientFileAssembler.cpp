/*
  Copyright 2007 Bradley Arsenault

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

#include "BinaryStream.h"
#include "FileManager.h"
#include "NetMessage.h"
#include "StreamBackend.h"
#include "Stream.h"
#include "Toolkit.h"
#include "YOGClientFileAssembler.h"
#include "YOGClient.h"

using namespace GAGCore;
using boost::static_pointer_cast;

YOGClientFileAssembler::YOGClientFileAssembler(boost::weak_ptr<YOGClient> client, Uint16 fileID)
	: client(client), fileID(fileID)
{
	oBackend = NULL;
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
	boost::shared_ptr<YOGClient> nclient(client);
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



void YOGClientFileAssembler::startReceivingFile(std::string mapname)
{
	filename=mapname;
	oBackend = new MemoryStreamBackend;
	ostream.reset(new BinaryOutputStream(oBackend));
	mode=ReceivingFile;
	finished=0;
}



void YOGClientFileAssembler::handleMessage(boost::shared_ptr<NetMessage> message)
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
				//Write from the buffer, obackend, to the file
				BinaryOutputStream* fstream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename+".gz"));
				ostream->seekFromEnd(0);
				fstream->write(oBackend->getBuffer(), ostream->getPosition(), "");
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
	boost::shared_ptr<YOGClient> nclient(client);
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
	boost::shared_ptr<YOGClient> nclient(client);
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
	boost::shared_ptr<YOGClient> nclient(client);
	shared_ptr<NetSendFileChunk> message(new NetSendFileChunk(istream, fileID));
	finished += message->getChunkSize();
	nclient->sendNetMessage(message);
}

