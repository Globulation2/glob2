// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "Version.h"
#include "MapHeader.h"
#include "Game.h"
#include <algorithm>
#include <map>
#include "FileManager.h"

MapHeader::MapHeader()
{
	reset();
}



void MapHeader::reset()
{
	versionMajor = VERSION_MAJOR;
	versionMinor = VERSION_MINOR;
	numberOfTeams = 0;
	mapName = "";
	mapOffset = 0;
	isSavedGame=false;
	resetGameSHA1();
}



bool MapHeader::load(GAGCore::InputStream *stream)
{
	///First, check if its an old format map
	Uint32 pos = stream->getPosition();
	char* signature[4];
	stream->read(signature, 4, "signature");
	if(memcmp(signature, "SEGb",4) == 0)
	{
		return false;
	}
	stream->seekFromStart(pos);

	stream->readEnterSection("MapHeader");
	mapName = stream->readText("mapName");
	versionMajor = stream->readSint32("versionMajor");
	versionMinor = stream->readSint32("versionMinor");

	numberOfTeams = stream->readSint32("numberOfTeams");
	mapOffset = stream->readUint32("mapOffset");
	isSavedGame = stream->readUint8("isSavedGame");

	if(numberOfTeams > Team::MAX_COUNT)
	{
		return false;
	}

	if(versionMinor==67)
		stream->readUint32("checksum");
	
	if(versionMinor>=68)
	{
		stream->read(SHA1, 20, "SHA1");
	}
	
	stream->readEnterSection("teams");
	for(int i=0; i<numberOfTeams; ++i)
	{
		stream->readEnterSection(i);
		teams[i].load(stream, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}


	
void MapHeader::save(GAGCore::OutputStream *stream, size_t *sha1Position) const
{
	stream->writeEnterSection("MapHeader");
	stream->writeText(mapName, "mapName");
	stream->writeSint32(VERSION_MAJOR, "versionMajor");
	stream->writeSint32(VERSION_MINOR, "versionMinor");
	stream->writeSint32(numberOfTeams, "numberOfTeams");
	stream->writeUint32(mapOffset, "mapOffset");
	stream->writeUint8(isSavedGame, "isSavedGame");
	if (sha1Position)
		*sha1Position = stream->getPosition();
	stream->write(SHA1, 20, "SHA1");
	stream->writeEnterSection("teams");
	for(int i=0; i<numberOfTeams; ++i)
	{
		stream->writeEnterSection(i);
		teams[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



Sint32 MapHeader::getVersionMajor() const
{
	return versionMajor;
}



Sint32 MapHeader::getVersionMinor() const
{
	return versionMinor;
}



Sint32 MapHeader::getNumberOfTeams() const
{
	return numberOfTeams;
}



void MapHeader::setNumberOfTeams(Sint32 teamNum)
{
	numberOfTeams = teamNum;
}



const std::string& MapHeader::getMapName() const
{
	return mapName;
}



std::string MapHeader::getFileName(bool isCampaignMap, bool isReplay) const
{
	if(isReplay)
		return glob2NameToFilename("replays", mapName, "replay");
	else if(isCampaignMap)
		return glob2NameToFilename("campaigns", mapName, "map");
	else if (!isSavedGame)
		return glob2NameToFilename("maps", mapName, "map");
	else
		return glob2NameToFilename("games", mapName, "game");
}



void MapHeader::setMapName(const std::string& newMapName)
{
	mapName = newMapName;
}



Uint32 MapHeader::getMapOffset() const
{
	return mapOffset;
}



void MapHeader::setMapOffset(Uint32 newMapOffset)
{
	mapOffset = newMapOffset;
}



BaseTeam& MapHeader::getBaseTeam(const int n)
{
	assert(n>=0 && n<Team::MAX_COUNT);
	return teams[n];
}



const BaseTeam& MapHeader::getBaseTeam(const int n) const
{
	assert(n>=0 && n<Team::MAX_COUNT);
	return teams[n];
}



bool MapHeader::getIsSavedGame() const
{
	return isSavedGame;
}



void MapHeader::setIsSavedGame(bool newIsSavedGame)
{
	isSavedGame = newIsSavedGame;
}



void MapHeader::setGameSHA1(Uint8 SHA1sum[20])
{
	for(int i=0; i<20; ++i)
		SHA1[i] = SHA1sum[i];
}



Uint8* MapHeader::getGameSHA1()
{
	return SHA1;
}



void MapHeader::resetGameSHA1()
{
	for(int i=0; i<20; ++i)
		SHA1[i] = 0;
}



Uint32 MapHeader::checkSum() const
{
	// `cs` is signed `Sint32` so the open-coded `(cs<<31)|(cs>>1)` rotate
	// uses arithmetic right-shift (sign-extending). See the matching note
	// in Building::checkSum — do NOT replace with the unsigned `rotr1`
	// helper or the network checksum diverges.
	Sint32 cs = 0;
	cs^=versionMajor;
	cs^=versionMinor;
	cs^=numberOfTeams;
	cs=(cs<<31)|(cs>>1);
	return cs;
}



bool MapHeader::operator!=(const MapHeader& rhs) const
{
	if( rhs.numberOfTeams != numberOfTeams ||
		rhs.mapOffset != mapOffset ||
		rhs.isSavedGame != isSavedGame ||
		rhs.mapName != mapName ||
		!std::equal(SHA1, SHA1+20, rhs.SHA1))
		return true;
	return false;
}



bool MapHeader::operator==(const MapHeader& rhs) const
{
	if( rhs.numberOfTeams == numberOfTeams &&
		rhs.mapOffset == mapOffset &&
		rhs.isSavedGame == isSavedGame &&
		rhs.mapName == mapName &&
		std::equal(SHA1, SHA1+20, rhs.SHA1))
		return true;
	return false;
}


std::string glob2FilenameToName(const std::string& filename)
{
	// Strip a ".gz" container suffix first so "Foo.map.gz"/"Foo.game.gz" resolve
	// their display name exactly like the uncompressed "Foo.map"/"Foo.game" did.
	std::string trimmed = filename;
	static const std::string gzSuffix = ".gz";
	if (trimmed.size() >= gzSuffix.size() && trimmed.compare(trimmed.size()-gzSuffix.size(), gzSuffix.size(), gzSuffix) == 0)
		trimmed.resize(trimmed.size() - gzSuffix.size());

	std::string mapName;
	if(trimmed.find(".game")!=std::string::npos)
		mapName=trimmed.substr(trimmed.find("/")+1, trimmed.size()-6-trimmed.find("/"));
	else if(trimmed.find(".replay")!=std::string::npos)
		mapName=trimmed.substr(trimmed.find("/")+1, trimmed.size()-8-trimmed.find("/"));
	else
		mapName=trimmed.substr(trimmed.find("/")+1, trimmed.size()-5-trimmed.find("/"));
	size_t pos = mapName.find("_");
	while(pos != std::string::npos)
	{
		mapName.replace(pos, 1, " ");
		pos = mapName.find("_");
	}
	return mapName;
}

template<typename It, typename T>
class contains
{
public:
	contains(const It from, const It to) : from(from), to(to) {}
	bool operator()(T d) { return (std::find(from, to, d) != to); }
private:
	const It from;
	const It to;
};

std::string glob2NameToFilename(const std::string& dir, const std::string& name, const std::string& extension)
{
	const char* pattern = " \t";
	const char* endPattern = strchr(pattern, '\0');
	std::string fileName = name;
	std::replace_if(fileName.begin(), fileName.end(), contains<const char*, char>(pattern, endPattern), '_');
	std::string fullFileName = dir;
	fullFileName += DIR_SEPARATOR + fileName;
	if (extension != "" && extension != "\0")
	{
		fullFileName += '.';
		fullFileName += extension;
	}
	return fullFileName;
}

namespace
{
	bool endsWithGz(const std::string& path)
	{
		static const std::string suffix = ".gz";
		return path.size() >= suffix.size() && path.compare(path.size()-suffix.size(), suffix.size(), suffix) == 0;
	}
}

std::string glob2GzipWritePath(const std::string& path)
{
	return endsWithGz(path) ? path : path + ".gz";
}

bool glob2IsGzipPath(const std::string& path)
{
	return endsWithGz(path);
}

std::string glob2PreferGzipReadPath(GAGCore::FileManager& files, const std::string& path)
{
	if (endsWithGz(path))
		return path;
	const std::string gzipped = path + ".gz";
	return files.exists(gzipped) ? gzipped : path;
}

GAGCore::StreamBackend *glob2OpenMapOrSaveInputStreamBackend(GAGCore::FileManager& files, const std::string& path)
{
	return files.openInflatingInputStreamBackend(glob2PreferGzipReadPath(files, path));
}

std::vector<std::string> glob2ListMapOrSaveFiles(GAGCore::FileManager& files, const std::string& dir, const std::string& baseExtension)
{
	std::vector<std::string> result;
	std::map<std::string, size_t> indexByName;
	auto scan = [&](const std::string& extension, bool isGzip)
	{
		if (!files.initDirectoryListing(dir.c_str(), extension, false))
			return;
		std::string fileName;
		while (!(fileName = files.getNextDirectoryEntry()).empty())
		{
			std::string name = isGzip ? fileName.substr(0, fileName.size()-3) : fileName;
			std::string fullFileName = dir + DIR_SEPARATOR + fileName;
			auto it = indexByName.find(name);
			if (it != indexByName.end())
				result[it->second] = fullFileName; // the later (".gz") scan wins
			else
			{
				indexByName[name] = result.size();
				result.push_back(fullFileName);
			}
		}
	};
	scan(baseExtension, false);
	scan(baseExtension + ".gz", true);
	return result;
}

