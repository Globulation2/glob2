// SPDX-License-Identifier: GPL-3.0-or-later
#include "MapTiling.h"
#include "MapHeader.h"
#include "Game.h"
#include "Team.h"
#include "Utilities.h"
#include "BinaryStream.h"
#include "FormatableString.h"
#include "Toolkit.h"
#include "FileManager.h"
#include <memory>
#include <cstring>

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
		// colony n is placed when it is the first colony of its stretch of total/placed
		for (int i = 0; i < placed; i++)
		{
			const int at = (i * total) / placed;
			if (at == n)
				return i % teams;
			if (at > n)
				break;
		}
		return -1;
	}

	MapInfo readMapInfo(const std::string& fileName)
	{
		MapInfo info;
		std::unique_ptr<GAGCore::InputStream> in(new GAGCore::BinaryInputStream(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(fileName)));
		if (in->isEndOfStream() || !info.header.load(in.get()) || !in->canSeek())
			return info;
		// the map section starts with its signature and the size shifts, see Map::load
		in->seekFromStart(info.header.getMapOffset());
		in->readEnterSection("Map");
		char signature[4];
		in->read(signature, 4, "signatureStart");
		if (memcmp(signature, "MapB", 4) != 0)
			return info;
		info.w = 1 << in->readSint32("wDec");
		info.h = 1 << in->readSint32("hDec");
		info.valid = info.header.getNumberOfTeams() > 0 && info.w > 0 && info.h > 0;
		return info;
	}

	bool fits(const MapInfo& map, int rx, int ry, int teams, int swarms)
	{
		if (!map.valid || map.w * rx > MAX_MAP_SIDE || map.h * ry > MAX_MAP_SIDE)
			return false;
		const int colonies = colonyCount(map.header.getNumberOfTeams(), rx, ry);
		return teams >= 1 && teams <= colonies && swarms >= 1 && swarms * teams <= colonies;
	}

	void adjust(const MapInfo& map, int& rx, int& ry, int& teams, int& swarms)
	{
		if (!map.valid)
			return;
		const int mapTeams = map.header.getNumberOfTeams();
		while (map.w * rx > MAX_MAP_SIDE && rx > 1) rx /= 2;
		while (map.h * ry > MAX_MAP_SIDE && ry > 1) ry /= 2;
		// no colony count chosen yet: one per colony of the repeated map
		if (teams < 1)
			teams = std::min<int>(Team::MAX_COUNT, colonyCount(mapTeams, rx, ry));
		// grow the repeat, shorter side first, until the colonies wanted fit
		while (colonyCount(mapTeams, rx, ry) < teams)
		{
			const bool canX = map.w * rx * 2 <= MAX_MAP_SIDE, canY = map.h * ry * 2 <= MAX_MAP_SIDE;
			if (!canX && !canY)
				break;
			if ((map.w * rx <= map.h * ry && canX) || !canY) rx *= 2; else ry *= 2;
		}
		teams = std::max(1, std::min<int>(teams, std::min<int>(Team::MAX_COUNT, colonyCount(mapTeams, rx, ry))));
		if (swarms < 1)
			swarms = colonyCount(mapTeams, rx, ry) / teams;
		swarms = std::max(1, std::min(swarms, colonyCount(mapTeams, rx, ry) / teams));
	}

	MapHeader writeTiledMap(const MapHeader& source, int rx, int ry, int teams, int perTeam)
	{
		MapHeader failed;
		failed.setNumberOfTeams(0);
		std::unique_ptr<GAGCore::InputStream> in(new GAGCore::BinaryInputStream(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(source.getFileName())));
		if (in->isEndOfStream())
			return failed;
		Game game(NULL, NULL);
		if (!game.load(in.get()))
			return failed;
		if (!game.tileForPlay(rx, ry, teams, perTeam))
			return failed;
		const int placed = placedColonyCount(colonyCount(source.getNumberOfTeams(), rx, ry), teams, perTeam);
		std::string name = FormatableString("%0 %1x%2 %3t%4c").arg(source.getMapName()).arg(rx).arg(ry).arg(teams).arg(placed / teams);
		MapHeader header = game.mapHeader;
		header.setMapName(name);
		header.setIsSavedGame(false);
		std::unique_ptr<GAGCore::OutputStream> out(new GAGCore::BinaryOutputStream(GAGCore::Toolkit::getFileManager()->openOutputStreamBackend(header.getFileName())));
		if (out->isEndOfStream())
			return failed;
		game.save(out.get(), true, name);
		out.reset();
		// read the header back so it carries the checksum of the file as written
		std::unique_ptr<GAGCore::InputStream> check(new GAGCore::BinaryInputStream(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(header.getFileName())));
		MapHeader written;
		if (check->isEndOfStream() || !written.load(check.get()))
			return failed;
		written.setMapName(name);
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
