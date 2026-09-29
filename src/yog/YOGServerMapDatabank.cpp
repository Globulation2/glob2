// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "BinaryStream.h"
#include "FileManager.h"
#include "MapDatabaseMessages.h"
#include "Toolkit.h"
#include "YOGServer.h"
#include "YOGServerMapDatabank.h"
#include "Version.h"
#include <filesystem>

using namespace GAGCore;

YOGServerMapDatabank::YOGServerMapDatabank(YOGServer* server)
	: server(server)
{
	currentMapID = 0;
}



void YOGServerMapDatabank::addMap(const YOGDownloadableMapInfo& map)
{
	int fileID = server->getFileDistributionManager().allocateFileDistributor();
	server->getFileDistributionManager().getDistributor(fileID)->loadFromLocally(map.getMapHeader().getFileName());
	YOGDownloadableMapInfo nmap(map);
	nmap.setFileID(fileID);
	nmap.setMapID(currentMapID);
	maps.push_back(nmap);
	currentMapID+=1;
	save();
}



void YOGServerMapDatabank::removeMap(const std::string& map)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == map)
		{
			server->getFileDistributionManager().removeDistributor(i->getFileID());
			maps.erase(i);
			save();
			return;
		}
	}
}



bool YOGServerMapDatabank::doesMapExist(const std::string& map)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == map)
		{
			return true;
		}
	}
	return false;
}



YOGMapUploadRefusalReason YOGServerMapDatabank::canReceiveFromPlayer(const YOGDownloadableMapInfo& map)
{
	for(std::vector<std::tuple<YOGDownloadableMapInfo, int> >::iterator i = uploadingMaps.begin(); i!=uploadingMaps.end(); ++i)
	{
		if(std::get<0>(*i).getMapHeader().getMapName() == map.getMapHeader().getMapName())
			return YOGMapUploadReasonMapNameAlreadyExists;
	}
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapHeader().getMapName() == map.getMapHeader().getMapName())
			return YOGMapUploadReasonMapNameAlreadyExists;
	}
	return YOGMapUploadReasonUnknown;
}



Uint16 YOGServerMapDatabank::receiveMapFromPlayer(const YOGDownloadableMapInfo& map, std::shared_ptr<YOGServerPlayer> player)
{
	int fileID = server->getFileDistributionManager().allocateFileDistributor();
	server->getFileDistributionManager().getDistributor(fileID)->loadFromPlayer(player);
	uploadingMaps.push_back(std::make_tuple(map, fileID));
	return fileID;
}



void YOGServerMapDatabank::sendMapListToPlayer(std::shared_ptr<YOGServerPlayer> player)
{
	std::shared_ptr<NetDownloadableMapInfos> infos(new NetDownloadableMapInfos(maps));
	player->sendMessage(infos);
}



void YOGServerMapDatabank::sendMapThumbnailToPlayer(Uint16 mapID, std::shared_ptr<YOGServerPlayer> player)
{
	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapID() == mapID)
		{
			MapThumbnail thumbnail = loadThumbnail(i->getMapHeader().getMapName(), i->getMapHeader().getFileName());
			std::shared_ptr<NetSendMapThumbnail> infos(new NetSendMapThumbnail(mapID, thumbnail));
			player->sendMessage(infos);
			return;
		}
	}
}



void YOGServerMapDatabank::submitRating(Uint16 mapID, Uint8 rating)
{
	///Don't accept ratings above 10
	if(rating > 10)
		return;

	for(std::vector<YOGDownloadableMapInfo>::iterator i = maps.begin(); i!=maps.end(); ++i)
	{
		if(i->getMapID() == mapID)
		{
			int r = i->getRatingTotal() + rating;
			int n  = i->getNumberOfRatings() + 1;
			i->setRatingTotal(r);
			i->setNumberOfRatings(n);
		}
	}
}



void YOGServerMapDatabank::update()
{
	for(std::vector<std::tuple<YOGDownloadableMapInfo, int> >::iterator i=uploadingMaps.begin(); i!=uploadingMaps.end();)
	{
		if(server->getFileDistributionManager().getDistributor(std::get<1>(*i))->areAllChunksLoaded())
		{
			server->getFileDistributionManager().getDistributor(std::get<1>(*i))->saveToFile(std::get<0>(*i).getMapHeader().getFileName());
			server->getFileDistributionManager().removeDistributor(std::get<1>(*i));
			addMap(std::get<0>(*i));
			Uint32 n = i - uploadingMaps.begin();
			uploadingMaps.erase(i);
			i = uploadingMaps.begin() + n;
		}
		else if(server->getFileDistributionManager().getDistributor(std::get<1>(*i))->wasUploadingCanceled())
		{
			server->getFileDistributionManager().removeDistributor(std::get<1>(*i));
			Uint32 n = i - uploadingMaps.begin();
			uploadingMaps.erase(i);
			i = uploadingMaps.begin() + n;
		}
		else
		{
			++i;
		}
	}
}



std::string YOGServerMapDatabank::getThumbtackFile(const std::string& mapName)
{
	return glob2NameToFilename("thumbnails", mapName, "thumbnail");
}



MapThumbnail YOGServerMapDatabank::loadThumbnail(const std::string& mapName, const std::string& fileName)
{
	MapThumbnail thumbnail;
	auto files = Toolkit::getFileManager();
	std::string revision;
	std::error_code error;
	for (unsigned i = 0; i < files->getDirCount(); ++i)
	{
		auto path = std::filesystem::path(files->getDir(i)) / fileName;
		auto time = std::filesystem::last_write_time(path, error);
		if (error) continue;
		auto size = std::filesystem::file_size(path, error);
		if (!error) { revision = std::to_string(static_cast<long long>(time.time_since_epoch().count())) + ":" + std::to_string(size); break; }
	}
	const auto cacheFile = getThumbtackFile(mapName) + ".v2";
	if (!revision.empty())
	{
		try
		{
			BinaryInputStream input(files->openInputStreamBackend(cacheFile));
			BinaryInputStream::CheckedReads checked(&input);
			if (input.isValid() && !input.isEndOfStream() && input.readText("revision") == revision)
				thumbnail.decodeData(&input, VERSION_MINOR);
		}
		catch (const std::exception&) { }
	}
	if (!thumbnail.isLoaded())
	{
		thumbnail.loadFromMap(fileName);
		if (thumbnail.isLoaded() && !revision.empty())
		{
			try
			{
				BinaryOutputStream output(files->openOutputStreamBackend(cacheFile));
				if (output.isValid()) { output.writeText(revision, "revision"); thumbnail.encodeData(&output); }
			}
			catch (const std::exception&) { }
		}
	}
	return thumbnail;
}



void YOGServerMapDatabank::load()
{
	InputStream* stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend("mapdatabank"));
	if(!stream->isEndOfStream())
	{
		Uint32 versionMinor = stream->readUint32("version");
		currentMapID = stream->readUint16("currentMapID");
		stream->readEnterSection("maps");
		Uint32 size = stream->readUint32("size");
		for(unsigned i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			YOGDownloadableMapInfo info;
			info.decodeData(stream, versionMinor);
			int fileID = server->getFileDistributionManager().allocateFileDistributor();
			server->getFileDistributionManager().getDistributor(fileID)->loadFromLocally(info.getMapHeader().getFileName());		
			info.setFileID(fileID);
			maps.push_back(info);
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	delete stream;
}


void YOGServerMapDatabank::save()
{
	OutputStream* stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("mapdatabank"));
	stream->writeUint32(VERSION_MINOR, "version");
	stream->writeUint16(currentMapID, "currentMapID");
	stream->writeEnterSection("maps");
	stream->writeUint32(maps.size(), "size");
	for(unsigned int i=0; i<maps.size(); ++i)
	{
		stream->writeEnterSection(i);
		maps[i].encodeData(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	delete stream;
}
