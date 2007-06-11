/*
  Copyright 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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

using namespace boost;
using namespace GAGCore;

MapAssembler::MapAssembler(boost::shared_ptr<YOGClient> client)
	: client(client)
{
	mode = NoTransfer;
	size = 0;
	finished=0;
}



void MapAssembler::update()
{
	
}



void MapAssembler::startSendingFile(std::string mapname)
{
	finished=0;
	istream.reset(new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(mapname)));
	istream->seekFromEnd(0);
	size=istream->getPosition();
	istream->seekFromStart(0);
	shared_ptr<NetSendFileInformation> message(new NetSendFileInformation(size));
	client->sendNetMessage(message);
	mode=SendingFile;
}



void MapAssembler::startRecievingFile(std::string mapname)
{
	ostream.reset(new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(mapname)));
	mode=RecivingFile;
	finished=0;
}



bool MapAssembler::isTransferComplete()
{
	if(mode == NoTransfer)
		return true;
	return false;
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
		sendNextChunk();
	}
	if(type == MNetSendFileChunk)
	{
		shared_ptr<NetSendFileChunk> info = static_pointer_cast<NetSendFileChunk>(message);
		Uint32 size = info->getChunkSize();
		Uint8* buffer = new Uint8[size];
		shared_ptr<GAGCore::InputStream> s(info->getStream());
		s->read(buffer, size, "data");
		ostream->write(buffer, size, "");
		requestNextChunk();
	}
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

