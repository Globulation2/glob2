// SPDX-License-Identifier: GPL-3.0-or-later
// Real-engine regression for repeating a map (Game::tileForPlay): every copy of every colony
// lands on the team MapTiling deals it to, with its buildings, units, painted forbidden, guard
// and clearing areas and its clearing flags' resource choices.
#include "GlobalContainer.h"
#include "BinaryStream.h"
#include "Building.h"
#include "BuildingType.h"
#include "FileManager.h"
#include "Game.h"
#include "MapTiling.h"
#include "Team.h"
#include "Toolkit.h"
#include "Unit.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

GlobalContainer* globalContainer = nullptr;

namespace
{
void require(bool ok, const char* message)
{
	if (!ok)
	{
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

bool load(Game& game, const char* file)
{
	std::unique_ptr<GAGCore::InputStream> in(new GAGCore::BinaryInputStream(GAGCore::Toolkit::getFileManager()->openInputStreamBackend(file)));
	return !in->isEndOfStream() && game.load(in.get());
}

int buildingsOf(const Team* team)
{
	int n = 0;
	for (int i = 0; i < Building::MAX_COUNT; i++)
		n += team->myBuildings[i] && team->myBuildings[i]->buildingState == Building::ALIVE;
	return n;
}

int unitsOf(const Team* team)
{
	int n = 0;
	for (int i = 0; i < Unit::MAX_COUNT; i++)
		n += team->myUnits[i] && !team->myUnits[i]->isDead;
	return n;
}

// The team bits of one kind of area on a tile.
Uint32 areaMask(const Map& map, int x, int y, int kind)
{
	Uint32 mask = 0;
	for (int t = 0; t < Team::MAX_COUNT; t++)
	{
		const Uint32 bit = Team::teamNumberToMask(t);
		const bool on = kind == 0 ? map.isForbidden(x, y, bit) : kind == 1 ? map.isGuardArea(x, y, bit) : map.isClearArea(x, y, bit);
		if (on)
			mask |= bit;
	}
	return mask;
}
}

int main(int argc, char** argv)
{
	SDL_SetMainReady();
	require(argc == 2 && std::string(argv[1]).find("glob2-save-test-") == 0, "usage: MapTilingHarness <disposable profile>");
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.load();

	Game game(nullptr, nullptr);
	require(load(game, "maps/balanced_for_2.map"), "load balanced_for_2");
	const int w0 = game.map.getW(), h0 = game.map.getH(), mapTeams = game.mapHeader.getNumberOfTeams();
	require(mapTeams == 2, "balanced_for_2 has two colonies");

	// Paint an area of each kind for team 0 and a forbidden tile for team 1, and give team 0 a
	// clearing flag that leaves wood alone.
	struct Paint { int x, y, team, kind; };
	const Paint paints[] = {{3, 5, 0, 0}, {4, 5, 0, 1}, {5, 5, 0, 2}, {10, 20, 1, 0}};
	for (const Paint& p : paints)
	{
		if (p.kind == 0)
			game.map.addForbidden(p.x, p.y, p.team);
		else if (p.kind == 1)
			game.map.addGuardArea(p.x, p.y, p.team);
		else
			game.map.addClearArea(p.x, p.y, p.team);
	}
	const int flagType = globals.buildingsTypes.getTypeNum("clearingflag", 0, false);
	require(flagType >= 0, "clearing flag type exists");
	Building* flag = game.addBuilding(20, 40, flagType, 0);
	require(flag != nullptr, "place a clearing flag");
	flag->clearingResources[WOOD] = false;

	int buildings[2], units[2];
	for (int t = 0; t < mapTeams; t++)
	{
		buildings[t] = buildingsOf(game.teams[t]);
		units[t] = unitsOf(game.teams[t]);
	}

	const int rx = 2, ry = 2, teams = 8;
	const int total = MapTiling::colonyCount(mapTeams, rx, ry);
	require(total == 8, "2 x 2 holds eight colonies");
	require(game.tileForPlay(rx, ry, teams, 1), "tileForPlay succeeds");
	require(game.map.getW() == w0 * rx && game.map.getH() == h0 * ry, "the map is repeated 2 x 2");
	require(game.mapHeader.getNumberOfTeams() == teams, "one team per colony");

	int n = 0;
	for (int j = 0; j < ry; j++)
		for (int i = 0; i < rx; i++)
			for (int t = 0; t < mapTeams; t++, n++)
			{
				const int k = MapTiling::teamForColony(n, total, teams, 1);
				require(k >= 0 && k < teams, "every colony is kept");
				require(buildingsOf(game.teams[k]) == buildings[t], "the copy keeps its colony's buildings");
				require(unitsOf(game.teams[k]) == units[t], "the copy keeps its colony's units");
				for (const Paint& p : paints)
					if (p.team == t)
						require(areaMask(game.map, p.x + i * w0, p.y + j * h0, p.kind) == Team::teamNumberToMask(k),
								"a painted area belongs to exactly the copy's team");
				if (t == 0)
				{
					int flags = 0;
					for (int b = 0; b < Building::MAX_COUNT; b++)
					{
						const Building* copy = game.teams[k]->myBuildings[b];
						if (copy && copy->typeNum == flagType)
						{
							flags++;
							require(!copy->clearingResources[WOOD] && copy->clearingResources[WHEAT], "the clearing flag keeps its choice");
						}
					}
					require(flags == 1, "the copy has its clearing flag");
				}
			}
	std::puts("PASS a repeated map deals each colony, its areas and its flag settings to one team");
	return 0;
}
