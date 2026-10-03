// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapTiling.h"
#include "MapHeader.h"
#include "Game.h"
#include "MapThumbnail.h"
#include "Team.h"
#include "Utilities.h"
#include "BinaryStream.h"
#include "FormatableString.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <cstring>

namespace
{
	// FileManager's writable profile exists on native, Android, iOS and the
	// browser. System /tmp and TEMP are unavailable on several of those hosts.
	// Keep generated maps outside the authored library, and retain the file
	// while a lobby, editor or transfer still holds its header.
	std::string generatedTiledMapPath()
	{
		static unsigned counter = 0;
		auto &files = *GAGCore::Toolkit::getFileManager();
		files.addWriteSubdir("generated");
		return files.getDir(0) + "/generated/tiled-" + std::to_string(SDL_GetPerformanceCounter()) +
			"-" + std::to_string(counter++) + ".map";
	}
}

namespace MapTiling
{
	bool isActive(int rx, int ry, int teams, int mapTeams)
	{
		return rx > 1 || ry > 1 || (teams > 0 && teams != mapTeams);
	}

	int colonyCount(int mapTeams, int rx, int ry)
	{
		return mapTeams * rx * ry;
	}

	int placedColonyCount(int total, int teams, int perTeam)
	{
		if (teams < 1)
			return 0;
		int share = total / teams;
		if (perTeam > 0 && perTeam < share)
			share = perTeam;
		return share * teams;
	}

	int teamForColony(int n, int total, int teams, int perTeam)
	{
		const int placed = placedColonyCount(total, teams, perTeam);
		if (n < 0 || n >= total || placed < 1)
			return -1;
		// Invert floor(i * total / placed) without scanning every kept base.
		const int index = (n * placed + total - 1) / total;
		return index < placed && (index * total) / placed == n ? index % teams : -1;
	}

	MapInfo readMapInfo(const std::string& fileName)
	{
		MapInfo info;
		std::unique_ptr<GAGCore::InputStream> in(new GAGCore::BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), fileName)));
		if (in->isEndOfStream() || !info.header.load(in.get()) || !in->canSeek())
			return info;
		// the map section starts with its signature and the size shifts, see Map::load
		in->seekFromStart(info.header.getMapOffset());
		in->readEnterSection("Map");
		char signature[4];
		in->read(signature, 4, "signatureStart");
		if (memcmp(signature, "MapB", 4) != 0)
			return info;
		const int wDec = in->readSint32("wDec"), hDec = in->readSint32("hDec");
		// Imported files are untrusted; validate shifts before evaluating them.
		if (wDec < 5 || wDec > 9 || hDec < 5 || hDec > 9)
			return info;
		info.w = 1 << wDec;
		info.h = 1 << hDec;
		info.valid = !info.header.getIsSavedGame() && info.header.getNumberOfTeams() > 0 && info.header.getNumberOfTeams() <= Team::MAX_COUNT;
		return info;
	}

	bool fits(const MapInfo& map, int rx, int ry, int teams, int swarms)
	{
		if (!map.valid || rx < 1 || ry < 1 || (rx & (rx - 1)) || (ry & (ry - 1)) ||
			rx > MAX_MAP_SIDE / map.w || ry > MAX_MAP_SIDE / map.h)
			return false;
		const int colonies = colonyCount(map.header.getNumberOfTeams(), rx, ry);
		// a count of 0 was not chosen and follows the map, so it never fails
		if (teams < 1)
			return true;
		if (teams > colonies)
			return false;
		return swarms < 1 || swarms * teams <= colonies;
	}


	//! Loads the map of `source` into `game` and repeats it; false when either step fails.
	static bool loadTiled(const MapHeader& source, int rx, int ry, int teams, int perTeam, Game& game)
	{
		std::unique_ptr<GAGCore::InputStream> in(new GAGCore::BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), source.getFileName())));
		if (in->isEndOfStream())
			return false;
		if (!game.load(in.get()))
			return false;
		return game.tileForPlay(rx, ry, teams, perTeam);
	}

	bool tiledThumbnail(const MapHeader& source, int rx, int ry, int teams, int perTeam, MapThumbnail& thumbnail)
	{
		Game game(nullptr, nullptr);
		if (!loadTiled(source, rx, ry, teams, perTeam, game))
			return false;
		thumbnail.loadFromMap(game.map, game.mapHeader);
		return true;
	}

	MapHeader writeTiledMap(const MapHeader& source, int rx, int ry, int teams, int perTeam)
	{
		MapHeader failed;
		failed.setNumberOfTeams(0);
		Game game(nullptr, nullptr);
		if (!loadTiled(source, rx, ry, teams, perTeam, game))
			return failed;
		// tileForPlay accepts zero as an automatic player count. Use the
		// resolved count for the filename and the equal-share calculation too.
		teams = game.mapHeader.getNumberOfTeams();
		const int placed = placedColonyCount(colonyCount(source.getNumberOfTeams(), rx, ry), teams, perTeam);
		std::string name = FormattableString("%0 %1x%2 %3t%4c").arg(source.getMapName()).arg(rx).arg(ry).arg(teams).arg(placed / teams);
		// Store a complete ordinary map atomically. The override is process-local;
		// a remote client receives the serialized bytes through normal transfer.
		const std::string path = generatedTiledMapPath();
		if (!GAGCore::Toolkit::getFileManager()->writeAtomically(path,
			[&](GAGCore::OutputStream &out) { game.save(&out, true, name); }))
			return failed;
		// read the header back so it carries the checksum of the file as written
		std::unique_ptr<GAGCore::InputStream> check(new GAGCore::BinaryInputStream(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(path)));
		MapHeader written;
		if (check->isEndOfStream() || !written.load(check.get()))
			return failed;
		written.setMapName(name);
		written.setFileNameOverride(path);
		return written;
	}

	std::vector<int> repeatOptions(int side)
	{
		std::vector<int> options;
		for (int factor = 1; side > 0 && side * factor <= MAX_MAP_SIDE; factor *= 2)
			options.push_back(factor);
		return options;
	}

	MapHeader tiledHeader(const MapHeader& source, int rx, int ry, int teams)
	{
		MapHeader header = source;
		const int mapTeams = source.getNumberOfTeams();
		if (!isActive(rx, ry, teams, mapTeams) || mapTeams < 1)
			return header;
		if (teams < 1)
			teams = std::min<int>(Team::MAX_COUNT, colonyCount(mapTeams, rx, ry));
		if (teams > colonyCount(mapTeams, rx, ry))
			teams = colonyCount(mapTeams, rx, ry);
		header.setNumberOfTeams(teams);
		for (int k = 0; k < teams; k++)
		{
			// team k's first colony is colony k, which comes from map team k mod mapTeams
			BaseTeam& team = header.getBaseTeam(k);
			team = source.getBaseTeam(k % mapTeams);
			team.teamNumber = k;
			team.numberOfPlayer = 0;
			team.playersMask = 0;
			float r, g, b;
			Utilities::HSVtoRGB(&r, &g, &b, (static_cast<float>(k) * TEAM_COLOR_HUE_DEGREES) / static_cast<float>(teams), Team::TEAM_COLOR_SATURATION, Team::TEAM_COLOR_VALUE);
			team.color = GAGCore::Color(static_cast<Uint8>(Team::COLOR_CHANNEL_MAX * r), static_cast<Uint8>(Team::COLOR_CHANNEL_MAX * g), static_cast<Uint8>(Team::COLOR_CHANNEL_MAX * b));
		}
		return header;
	}
}
