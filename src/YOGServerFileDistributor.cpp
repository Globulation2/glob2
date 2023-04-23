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
#include "YOGServerFileDistributor.h"
#include "YOGServerPlayer.h"

using namespace GAGCore;
using boost::static_pointer_cast;

YOGServerFileDistributor::YOGServerFileDistributor(Uint16 fileID)
	: fileID(fileID), startedLoading(false), downloadFromPlayerCanceled(false)
{

}



void YOGServerFileDistributor::loadFromLocally(const std::string& file)
{
	fileName = file;
}



void YOGServerFileDistributor::loadFromPlayer(boost::shared_ptr<YOGServerPlayer> nplayer)
{
	player = nplayer;
}



void YOGServerFileDistributor::saveToFile(const std::string& file)
{
	boost::shared_ptr<BinaryOutputStream> stream(new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(file+".gz")));
	for(unsigned int i=0; i<chunks.size(); ++i)
	{
		stream->write(chunks[i]->getBuffer(), chunks[i]->getChunkSize(), "");
	}
	stream.reset();
	//unzip file
	Toolkit::getFileManager()->gunzip(file+".gz", file);
}



bool YOGServerFileDistributor::areAllChunksLoaded()
{
	if(!fileInfo)
		return false;
	Uint32 total = 0;
	for(unsigned int i=0; i<chunks.size(); ++i)
	{
		total += chunks[i]->getChunkSize();
	}
	if(total == fileInfo->getFileSize())
		return true;
	return false;
}



bool YOGServerFileDistributor::wasUploadingCanceled()
{
	return downloadFromPlayerCanceled;
}



void YOGServerFileDistributor::update()
{
	boost::posix_time::ptime localtime = boost::posix_time::second_clock::local_time();
	for(std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, boost::posix_time::ptime, int> >::iterator i = players.begin(); i!=players.end();)
	{
		if(!i->get<0>()->isConnected())
		{
			i = players.erase(i);
			continue;
		}

		if(i->get<2>() == 0 && fileInfo)
		{
			i->get<0>()->sendMessage(fileInfo);
			i->get<2>() = 1;
		}
		else if(i->get<2>() == 0) {
			// WORKAROUND
			continue;
		}
		else if(i->get<2>()-1 < (int)chunks.size() && i->get<1>() < localtime)
		{
			i->get<0>()->sendMessage(chunks[i->get<2>()-1]);
			i->get<2>() += 1;
			i->get<1>() = localtime + boost::posix_time::microseconds(100);
		}
		++i;
	}
}



void YOGServerFileDistributor::addMapRequestee(boost::shared_ptr<YOGServerPlayer> player)
{
	garunteeDataRequested();
	players.push_back(boost::make_tuple(player, boost::posix_time::second_clock::local_time(), 0));
}



void YOGServerFileDistributor::removeMapRequestee(boost::shared_ptr<YOGServerPlayer> player)
{
	for(std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, boost::posix_time::ptime, int> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(i->get<0>() == player)
		{
			players.erase(i);
			return;
		}
	}
}



void YOGServerFileDistributor::handleMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> nplayer)
{
	///This ignores certain messages that must come from the person uploading the map
	Uint8 messageType = message->getMessageType();
	if(messageType == MNetSendFileInformation && nplayer == player)
	{
		fileInfo = static_pointer_cast<NetSendFileInformation>(message);
	}
	else if(messageType == MNetSendFileChunk && nplayer == player)
	{
		chunks.push_back(static_pointer_cast<NetSendFileChunk>(message));
	}
	else if(messageType == MNetCancelSendingFile && nplayer == player)
	{
		chunks.clear();
		fileInfo.reset();
		downloadFromPlayerCanceled = true;
	}
}


void YOGServerFileDistributor::loadDataFromFile()
{
	if(!startedLoading)
	{
		startedLoading=true;
		Toolkit::getFileManager()->gzip(fileName, fileName+".gz");
		boost::shared_ptr<BinaryInputStream> istream(new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(fileName+".gz")));
		istream->seekFromEnd(0);
		int size=istream->getPosition();
		istream->seekFromStart(0);
		fileInfo = boost::shared_ptr<NetSendFileInformation>(new NetSendFileInformation(size, fileID));
		
		int ammount=0;
		while(ammount < size)
		{
			boost::shared_ptr<NetSendFileChunk> message(new NetSendFileChunk(istream, fileID));
			ammount += message->getChunkSize();
			chunks.push_back(message);
		}
	}
}


void YOGServerFileDistributor::requestDataFromPlayer()
{
	if(!startedLoading)
	{
		shared_ptr<NetRequestFile> message(new NetRequestFile(fileID));
		player->sendMessage(message);
	}
}

void YOGServerFileDistributor::garunteeDataRequested()
{
	if(player)
		requestDataFromPlayer();
	else
		loadDataFromFile();
}


