// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "BinaryStream.h"
#include "FileManager.h"
#include "FileTransferMessages.h"
#include "MapHeader.h"
#include "Toolkit.h"
#include "YOGServerFileDistributor.h"
#include "YOGServerPlayer.h"

using namespace GAGCore;
using std::static_pointer_cast;

YOGServerFileDistributor::YOGServerFileDistributor(Uint16 fileID)
	: fileID(fileID), startedLoading(false), downloadFromPlayerCanceled(false)
{

}



void YOGServerFileDistributor::loadFromLocally(const std::string& file)
{
	fileName = file;
}



void YOGServerFileDistributor::loadFromPlayer(std::shared_ptr<YOGServerPlayer> nplayer)
{
	player = nplayer;
}



void YOGServerFileDistributor::saveToFile(const std::string& file)
{
	// The wire payload is always a single gzip layer of the map bytes; store it
	// directly as the new on-disk container (atomically, so a crash mid-write
	// can't leave a corrupt file) instead of unzipping to a raw file. An older
	// receiver's gunzip-to-raw still works on this same payload, so this is
	// compatible with peers running that code.
	Toolkit::getFileManager()->writeAtomically(file+".gz", [&](OutputStream& stream) {
		for(unsigned int i=0; i<chunks.size(); ++i)
			stream.write(chunks[i]->getBuffer(), chunks[i]->getChunkSize(), "");
	});
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
	for(std::vector<std::tuple<std::shared_ptr<YOGServerPlayer>, boost::posix_time::ptime, int> >::iterator i = players.begin(); i!=players.end();)
	{
		if(!std::get<0>(*i)->isConnected())
		{
			i = players.erase(i);
			continue;
		}

		if(std::get<2>(*i) == 0 && fileInfo)
		{
			std::get<0>(*i)->sendMessage(fileInfo);
			std::get<2>(*i) = 1;
		}
		else if(std::get<2>(*i) == 0)
		{
			// The header arrives through the next server update. Advance past
			// this recipient so waiting for it cannot stall the message pump.
			++i;
			continue;
		}
		else if(std::get<2>(*i)-1 < (int)chunks.size() && std::get<1>(*i) < localtime)
		{
			std::get<0>(*i)->sendMessage(chunks[std::get<2>(*i)-1]);
			std::get<2>(*i) += 1;
			std::get<1>(*i) = localtime + boost::posix_time::microseconds(100);
		}
		++i;
	}
}



void YOGServerFileDistributor::addMapRequestee(std::shared_ptr<YOGServerPlayer> player)
{
	guaranteeDataRequested();
	players.push_back(std::make_tuple(player, boost::posix_time::second_clock::local_time(), 0));
}



void YOGServerFileDistributor::removeMapRequestee(std::shared_ptr<YOGServerPlayer> player)
{
	for(std::vector<std::tuple<std::shared_ptr<YOGServerPlayer>, boost::posix_time::ptime, int> >::iterator i = players.begin(); i!=players.end(); ++i)
	{
		if(std::get<0>(*i) == player)
		{
			players.erase(i);
			return;
		}
	}
}



void YOGServerFileDistributor::handleMessage(std::shared_ptr<NetMessage> message, std::shared_ptr<YOGServerPlayer> nplayer)
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
		FileManager& files = *Toolkit::getFileManager();
		const std::string resolved = glob2PreferGzipReadPath(files, fileName);
		// A locally compressed map is already the wire payload; only a legacy raw
		// map needs gzipping once before it is sent.
		std::string gzipFile = resolved;
		if (!glob2IsGzipPath(resolved))
		{
			files.gzip(resolved, resolved+".gz");
			gzipFile = resolved+".gz";
		}
		std::shared_ptr<BinaryInputStream> istream(new BinaryInputStream(files.openInputStreamBackend(gzipFile)));
		istream->seekFromEnd(0);
		int size=istream->getPosition();
		istream->seekFromStart(0);
		fileInfo = std::shared_ptr<NetSendFileInformation>(new NetSendFileInformation(size, fileID));
		
		int amount=0;
		while(amount < size)
		{
			std::shared_ptr<NetSendFileChunk> message(new NetSendFileChunk(istream, fileID));
			amount += message->getChunkSize();
			chunks.push_back(message);
		}
	}
}


void YOGServerFileDistributor::requestDataFromPlayer()
{
	if(!startedLoading)
	{
		// All recipients share this upload, including guests who rejoin.
		startedLoading=true;
		shared_ptr<NetRequestFile> message(new NetRequestFile(fileID));
		player->sendMessage(message);
	}
}

void YOGServerFileDistributor::guaranteeDataRequested()
{
	if(player)
		requestDataFromPlayer();
	else
		loadDataFromFile();
}


