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
#include "GenerationRequest.h"
#include "GenerationService.h"
#include "GeneratorControls.h"
#include "MapTiling.h"
#include "Team.h"
#include "Toolkit.h"
#include "Unit.h"
#include "EngineFixtures.h"
#include "ChooseMapScreen.h"
#include "GUIMapPreview.h"
#include "FrontendTheme.h"
#include "ScopedEnvironment.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sstream>
#include <set>


namespace
{
void require(bool ok, const char* message)
{
	if (!ok)
	{
		FAIL(message);
	}
}

bool load(Game& game, const char* file)
{
	std::unique_ptr<GAGCore::InputStream> in(new GAGCore::BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*GAGCore::Toolkit::getFileManager(), file)));
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

TEST_SUITE("MapTiling")
{
TEST_CASE("copies colonies, painted areas and clearing settings [save-format]")
{
	glob2test::HeadlessGlobals globals;

	Game game(nullptr, nullptr);
	require(load(game, "maps/balanced_for_2.map.gz"), "load balanced_for_2");
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
	const int flagType = globals->buildingsTypes.getTypeNum("clearingflag", 0, false);
	require(flagType >= 0, "clearing flag type exists");
	Building* flag = game.addBuilding(20, 40, flagType, 0);
	require(flag != nullptr, "place a clearing flag");
	flag->clearingMaterials[WOOD] = false;
	flag->minWorkerLevelToFlag=2;
	for (int i=0; i<Unit::MAX_COUNT; ++i)
		if (auto* unit=game.teams[0]->myUnits[i]; unit && unit->typeNum==WORKER)
			unit->constructionLevel=2;

	int anchorX[2], anchorY[2];
	for (int t = 0; t < mapTeams; ++t)
		for (int i = 0; i < Building::MAX_COUNT; ++i)
			if (const auto *b = game.teams[t]->myBuildings[i]; b && b->type->unitProductionTime)
			{
				anchorX[t] = b->posX;
				anchorY[t] = b->posY;
				break;
			}
	auto copyCoordinate = [](int position, int anchor, int side, int copy, int repeats)
	{
		int offset = (position - anchor + side) % side;
		if (offset >= side / 2) offset -= side;
		return (anchor + copy * side + offset + side * repeats) % (side * repeats);
	};
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
	std::set<std::uint64_t> selectors;
	for (int t = 0; t < teams; ++t) {
		for (const auto* unit : game.teams[t]->liveUnits.entries())
			require(selectors.insert(unit->entityRandom.exportState().increment).second, "tiled units have distinct streams");
		for (const auto* building : game.teams[t]->liveBuildings.entries())
			require(selectors.insert(building->entityRandom.exportState().increment).second, "tiled buildings have distinct streams");
	}


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
						require(areaMask(game.map, copyCoordinate(p.x, anchorX[t], w0, i, rx), copyCoordinate(p.y, anchorY[t], h0, j, ry), p.kind) == Team::teamNumberToMask(k),
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
							require(!copy->clearingMaterials[WOOD] && copy->clearingMaterials[WHEAT], "the clearing flag keeps its choice");
							require(copy->minWorkerLevelToFlag==2,"the clearing flag keeps independent worker qualification");
						}
					}
					require(flags == 1, "the copy has its clearing flag");
					for (int u=0; u<Unit::MAX_COUNT; ++u)
						if (const auto* unit=game.teams[k]->myUnits[u]; unit && unit->typeNum==WORKER)
							require(unit->constructionLevel==2,"workers keep independent construction qualification");
				}
			}
	std::puts("PASS a repeated map deals each colony, its areas and its flag settings to one team");

	// The source of a repeat is often a 32 x 32 map: the editor offers that size, the lobby keeps 64.
	for (const auto& shared : GenerationRequest::sharedControls())
		if (shared.id == "width" || shared.id == "height")
		{
			require(shared.minimum == 6, "the lobby's smallest map is 64 tiles");
			require(editorSizeControl(shared).minimum == 5, "the editor's smallest map is 32 tiles");
		}
	GenerationRequest request;
	request.setMethodDefaults(GenerationRequest::eUNIFORM);
	request.wDec = request.hDec = 5;
	request.seed = 1;
	Game small(nullptr, nullptr);
	require(bool(GenerationService().generate(small, request)), "a 32 x 32 map generates");
	require(small.map.getW() == 32 && small.map.getH() == 32, "the generated map is 32 x 32");
	std::puts("PASS the editor can make the 32 x 32 map a repeat starts from");
}
TEST_CASE("rejects invalid requests without mutating the source [save-format]")
{
	glob2test::HeadlessGlobals globals;
	Game game(nullptr, nullptr);
	REQUIRE(load(game, "maps/balanced_for_2.map.gz"));
	const int originalWidth = game.map.getW();
	for (int factor : {0, -1, 3, 1024, 2147483647})
	{
		CHECK_FALSE(game.tileForPlay(factor, 1, 2, 1));
		CHECK(game.map.getW() == originalWidth);
		CHECK(game.mapHeader.getNumberOfTeams() == 2);
	}
	game.mapscript.setMapScript("// authored scenario");
	CHECK_FALSE(game.tileForPlay(2, 2, 4, 1));
	CHECK(game.map.getW() == originalWidth);
}

TEST_CASE("writes a compressed source as a reloadable ordinary map [save-format]")
{
	glob2test::HeadlessGlobals globals;
	const auto source = MapTiling::readMapInfo("maps/balanced_for_2.map.gz");
	REQUIRE(source.valid);
	CHECK(source.w == 64);
	CHECK(source.h == 64);
	CHECK_FALSE(MapTiling::fits(source, 3, 1, 2, 1));
	CHECK_FALSE(MapTiling::fits(source, 2147483647, 1, 2, 1));
	MapHeader input = source.header;
	input.setFileNameOverride("maps/balanced_for_2.map.gz");
	const auto written = MapTiling::writeTiledMap(input, 2, 2, 0, 1);
	REQUIRE(written.getNumberOfTeams() == 8);
	CHECK(written.getFileName().find("/generated/") != std::string::npos);
	Game reload(nullptr, nullptr);
	REQUIRE(load(reload, written.getFileName().c_str()));
	CHECK(reload.map.getW() == 128);
	CHECK(reload.map.getH() == 128);
	CHECK(reload.mapHeader.getNumberOfTeams() == 8);
	CHECK_FALSE(reload.mapHeader.getIsSavedGame());
	CHECK(reload.gameHeader.getNumberOfPlayers() == 0);
	for (int t = 0; t < 8; ++t)
	{
		CHECK(buildingsOf(reload.teams[t]) > 0);
		CHECK(unitsOf(reload.teams[t]) > 0);
	}
	GAGCore::Toolkit::getFileManager()->remove(written.getFileName());
	// Six of eight bases survive, evenly spaced and split three ways.
	CHECK(MapTiling::placedColonyCount(8, 3, 2) == 6);
	int counts[3] = {};
	for (int n = 0; n < 8; ++n)
	{
		const int team = MapTiling::teamForColony(n, 8, 3, 2);
		if (team >= 0) ++counts[team];
	}
	for (int count : counts) CHECK(count == 2);
}

TEST_CASE("wraps swimmers, buildings and team areas together across the source seam [save-format]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .loadDefaultRace = true});
	auto &game = world.game;
	REQUIRE(world.addBuilding("swarm", 2, 10));
	game.map.paintCell(63, 10, WATER);
	auto *swimmer = game.addUnit(63, 10, 0, WORKER, 3, 0, 0, 0);
	REQUIRE(swimmer);
	swimmer->level[WALK] = 0;
	game.map.addGuardArea(63, 10, 0);
	game.map.addClearArea(63, 11, 0);
	game.map.addForbidden(63, 12, 0);
	game.map.addFarmArea(63, 13, 0);
	REQUIRE(game.tileForPlay(2, 1, 2, 1));
	CHECK(game.map.getGroundUnit(127, 10) != NOGUID);
	CHECK(game.map.getGroundUnit(63, 10) != NOGUID);
	CHECK(Unit::GIDtoTeam(game.map.getGroundUnit(127, 10)) == 0);
	CHECK(Unit::GIDtoTeam(game.map.getGroundUnit(63, 10)) == 1);
	CHECK(game.map.isGuardArea(127, 10, Team::teamNumberToMask(0)));
	CHECK(game.map.isClearArea(127, 11, Team::teamNumberToMask(0)));
	CHECK(game.map.isForbidden(127, 12, Team::teamNumberToMask(0)));
	CHECK(game.map.isGuardArea(63, 10, Team::teamNumberToMask(1)));
	CHECK(game.map.isFarmArea(127, 13, Team::teamNumberToMask(0)));
	CHECK_FALSE(game.map.isFarmArea(63, 13, Team::teamNumberToMask(0)));
	CHECK(game.map.isFarmArea(63, 13, Team::teamNumberToMask(1)));
}

TEST_CASE("a repeated starting map has portable per-tick checksums [save-format][golden]")
{
	glob2test::HeadlessGlobals globals;
	Game game(nullptr, nullptr);
	REQUIRE(load(game, "maps/balanced_for_2.map.gz"));
	REQUIRE(game.tileForPlay(2, 2, 4, 2));
	GameHeader header;
	header.setRandomSeed(261);
	header.setNumberOfPlayers(4);
	for (int i = 0; i < 4; ++i)
	{
		auto &player = header.getBasePlayer(i);
		player.setNumber(i);
		player.setTeamNumber(i);
		player.type = BasePlayer::P_LOCAL;
		player.name = "repeat";
	}
	game.setGameHeader(header);
	glob2test::BoundGameRandom random(game);
	std::ostringstream checksums;
	for (int tick = 0; tick < 128; ++tick)
	{
		game.syncStep(0);
		checksums << tick << ' ' << game.checkSum(nullptr, nullptr, nullptr, true) << '\n';
	}
	glob2test::expectGolden("map-tiling/checksums.txt", checksums.str());
}

TEST_CASE("advanced controls preview, reset and accept the repeated map on touch [display][artifacts]")
{
	glob2test::ScopedEnvironment mobileUI("GLOB2_MOBILE_UI", "1");
	glob2test::HeadlessGlobals globals({.display = true, .loadStrings = true, .width = 390, .height = 844,
		.screenFlags = GAGCore::GraphicContext::PORTABLEGPU});
	FrontendTheme theme;
	FrontendScope frontend;
	ChooseMapScreen screen("maps", "map", false);
	screen.editMapParameters("maps/balanced_for_2.map.gz");
	screen.beginExecution(globalContainer->gfx);
	screen.paintFrame(SDL_GetTicks());
	auto *x = screen.host().find("tiling/x"), *y = screen.host().find("tiling/y");
	auto *bases = screen.host().find("tiling/bases");
	REQUIRE(x); REQUIRE(y); REQUIRE(bases);
	CHECK(x->accessibleText() == "1");
	CHECK(y->accessibleText() == "1");
	CHECK(x->bounds.y > bases->bounds.y);
	CHECK(y->bounds.y > x->bounds.y);
	auto activate = [&](const std::string &key, int direction)
	{
		screen.paintFrame(SDL_GetTicks());
		auto *control = screen.host().find(key);
		REQUIRE(control);
		control->activate(screen.host(), direction);
	};
	activate("tiling/x", 1);
	CHECK(screen.getMapHeader().getNumberOfTeams() == 4);
	activate("tiling/y", 1);
	CHECK(screen.getMapHeader().getNumberOfTeams() == 8);
	activate("tiling/reset", 0);
	CHECK(screen.getMapHeader().getNumberOfTeams() == 2);
	activate("tiling/x", 1);
	activate("tiling/y", 1);
	screen.paintFrame(SDL_GetTicks());
	// Let the normal thumbnail crossfade finish before capturing its pixels.
	SDL_Delay(MapPreview::TransitionDurationMs + 10);
	screen.paintFrame(SDL_GetTicks());
	const auto capture = glob2test::artifactDir() / "repeated-phone.bmp";
	globalContainer->gfx->printScreen(glob2test::artifactDirFromWorkingDirectory() + "/repeated-phone.bmp");
	globalContainer->gfx->nextFrame();
	REQUIRE(std::filesystem::exists(capture));
	activate("ok", 0);
	CHECK_FALSE(screen.isExecutionRunning());
	const auto written = screen.getMapHeader();
	screen.finishExecution();
	Game reload(nullptr, nullptr);
	REQUIRE(load(reload, written.getFileName().c_str()));
	CHECK(reload.map.getW() == 128);
	CHECK(reload.map.getH() == 128);
	CHECK(reload.mapHeader.getNumberOfTeams() == 8);
	GAGCore::Toolkit::getFileManager()->remove(written.getFileName());

	// An unchanged absolute generated path must remain usable, and the UI
	// must show the embedded title rather than a profile path or timestamp.
	auto source = MapTiling::readMapInfo("maps/balanced_for_2.map.gz");
	source.header.setFileNameOverride("maps/balanced_for_2.map.gz");
	const auto original = MapTiling::writeTiledMap(source.header, 1, 1, 2, 1);
	REQUIRE(original.getNumberOfTeams() == 2);
	ChooseMapScreen unchanged("maps", "map", false);
	unchanged.editMapParameters(original.getFileName());
	unchanged.beginExecution(globalContainer->gfx);
	unchanged.paintFrame(SDL_GetTicks());
	auto *ok = unchanged.host().find("ok");
	REQUIRE(ok);
	ok->activate(unchanged.host(), 0);
	CHECK_FALSE(unchanged.isExecutionRunning());
	CHECK(unchanged.getMapHeader().getFileName() == original.getFileName());
	CHECK(unchanged.getMapHeader().getMapName() == original.getMapName());
	unchanged.finishExecution();
	GAGCore::Toolkit::getFileManager()->remove(original.getFileName());
}

TEST_CASE("refuses over-capacity bases before replacing the source [save-format]")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.wDec = 6, .hDec = 6, .loadDefaultRace = true});
	REQUIRE(world.addBuilding("swarm", 2, 10));
	const int flagType = globals->buildingsTypes.getTypeNum("clearingflag", 0, false);
	for (int i = 1; i < Building::MAX_COUNT; ++i)
		REQUIRE(world.game.addBuilding(0, 0, flagType, 0));
	CHECK_FALSE(world.game.tileForPlay(2, 1, 1, 2));
	CHECK(world.game.map.getW() == 64);
	CHECK(world.game.mapHeader.getNumberOfTeams() == 1);
	CHECK(buildingsOf(world.game.teams[0]) == int(Building::MAX_COUNT));
}

}
