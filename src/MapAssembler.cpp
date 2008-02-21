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

#include "MapAssembler.h"
#include "Toolkit.h"
#include "FileManager.h"
#include "StreamBackend.h"

using namespace boost;
using namespace GAGCore;

MapAssembler::MapAssembler(boost::shared_ptr<YOGClient> client)
	: client(client)
{
	obackend = NULL;
	mode = NoTransfer;
	size = 0;
	finished=0;
}



void MapAssembler::update()
{
	
}



void MapAssembler::startSendingFile(std::string mapname)
{
	Toolkit::getFileManager()->gzip(mapname, mapname+".gz");
	finished=0;
	istream.reset(new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapname+".gz")));
	istream->seekFromEnd(0);
	size=istream->getPosition();
	istream->seekFromStart(0);
	shared_ptr<NetSendFileInformation> message(new NetSendFileInformation(size));
	client->sendNetMessage(message);
	mode=SendingFile;
}



void MapAssembler::startRecievingFile(std::string mapname)
{
	filename=mapname;
	obackend = new MemoryStreamBackend;
	ostream.reset(new BinaryOutputStream(obackend));
	mode=RecivingFile;
	finished=0;
}



void MapAssembler::handleMessage(boost::shared_ptr<NetMessage> message)
{
	Uint8 type = message->getMessageType();
	if(type == MNetSendFileInformation)
	{
		shared_ptr<NetSendFileInformation> info = static_pointer_cast<NetSendFileInformation>(message);
		size = info->getFileSize();
		requestNextChunk();
	}
	if(type == MNetRequestNextChunk)
	{
		//shared_ptr<NetRequestNextChunk> info = static_pointer_cast<NetRequestNextChunk>(message);
		if(finished < size)
			sendNextChunk();
		else
			mode=NoTransfer;
	}
	if(type == MNetSendFileChunk)
	{
		shared_ptr<NetSendFileChunk> info = static_pointer_cast<NetSendFileChunk>(message);
		Uint32 bsize = info->getChunkSize();
		const Uint8* buffer = info->getBuffer();
		ostream->write(buffer, bsize, "");
		finished+=bsize;
		if(finished<size)
			requestNextChunk();
		else
		{
			mode=NoTransfer;
			//Write from the buffer, obackend, to the file
			BinaryOutputStream* fstream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(filename+"2.gz"));
			fstream->write(obackend->getBuffer(), obackend->getPosition(), "file");
			//unzip file
			Toolkit::getFileManager()->gunzip(filename+"2.gz", filename+"2");
			delete fstream;
			ostream.reset();
		}
	}
}



Uint8 MapAssembler::getPercentage()
{
	if(size == 0)
		return 100;

	return (finished * 100) / size;
}



void MapAssembler::sendNextChunk()
{
	shared_ptr<NetSendFileChunk> message(new NetSendFileChunk(istream));
	finished += message->getChunkSize();
	client->sendNetMessage(message);
}



void MapAssembler::requestNextChunk()
{
	shared_ptr<NetRequestNextChunk> message(new NetRequestNextChunk);
	client->sendNetMessage(message);
}

