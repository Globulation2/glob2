// SPDX-License-Identifier: GPL-3.0-or-later
// Regression for wrapped footprints being erased by Game::load integrity repair.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

static void checkFootprint(const Game& game, const Building& building)
{
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
        {
            // Enumerate the footprint independently of the integrity predicate.
            bool covered = false;
            for (int by = 0; by < building.type->height; ++by)
                for (int bx = 0; bx < building.type->width; ++bx)
                    covered |= x == ((building.posX + bx + 32) % 32)
                            && y == ((building.posY + by + 32) % 32);
            require(game.map.getBuilding(x, y) == (covered ? building.gid : NOGBID),
                    "integrity must preserve exactly the wrapped building footprint");
        }
}

static void writeFixture(GAGCore::MemoryStreamBackend& bytes, const char* path)
{
    bytes.seekFromEnd(0);
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.getBuffer(), bytes.getPosition());
    output.close();
    require(!output.fail(), "write fixture file");
}
}

TEST_SUITE("BuildingFootprint")
{
	TEST_CASE("generated fixtures round-trip through binary saves [save-format]")
	{
		glob2test::HeadlessGlobals globals;
		// GLOB2_TEST_UPDATE_FIXTURES=1 (run_tests.py --update-fixtures) writes the raw save
		// under the artifact directory; gzip it to refresh test/fixtures/wrapped-building/reproducer.game.gz.
	    const int type = globals->buildingsTypes.getTypeNum("swarm", 0, false);
	    const int positions[][2] = {{8,8}, {-1,8}, {8,-1}, {-1,-1}, {31,31}};
	    for (const auto& position : positions)
	    {
	        GameGUI gui;
	        Game& game = gui.game;
	        game.map.setSize(5, 5, GRASS);
	        game.map.setGame(&game);
	        game.addTeam(0);
	        auto* team = game.teams[0];
	        auto* building = new Building(position[0], position[1], 0, type, team,
	                                      &globals->buildingsTypes, 0, 0);
	        team->myBuildings[0] = building;
	        game.map.setBuilding(building->posX, building->posY,
	                             building->type->width, building->type->height, building->gid);
	        // Both genuine repairs must keep working alongside wrapped preservation.
	        game.map.setBuilding(building->posX, building->posY, 1, 1, NOGBID);
	        game.map.setBuilding(20, 20, 1, 1, building->gid);
	        require(game.integrity(), "valid wrapped building with repairable occupancy");
	        checkFootprint(game, *building);
	        require(game.integrity(), "integrity must be idempotent");
	        checkFootprint(game, *building);

	        auto* bytes = new GAGCore::MemoryStreamBackend;
	        GAGCore::BinaryOutputStream writer(bytes);
	        game.save(&writer, false, "wrapped footprint regression");
	        if (glob2test::updatingFixtures() &&
	            position[0] == -1 && position[1] == -1)
	            writeFixture(*bytes, (glob2test::artifactDir() / "reproducer.game").string().c_str());
	        auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
	        copy->seekFromStart(0);
	        GAGCore::BinaryInputStream reader(copy);
	        GameGUI restoredGUI;
	        Game& loaded = restoredGUI.game;
	        require(loaded.load(&reader), "wrapped building save must load");
	        auto* restored = loaded.teams[0]->myBuildings[0];
	        checkFootprint(loaded, *restored);
	        for (bool swimming : {false, true})
	        {
	            int x = -100, y = -100, dx = 0, dy = 0;
	            require(restored->findGroundExit(&x, &y, &dx, &dy, swimming), "exit after loading");
	            require(x >= 0 && x < 32 && y >= 0 && y < 32, "exit coordinates are wrapped");
	            require(loaded.map.getBuilding(x - dx, y - dy) == restored->gid,
	                    "exit direction points outward from an occupied building tile");
	            require(loaded.map.isFreeForGroundUnit(x, y, swimming, 1), "exit is free");
	        }
	    }
	}
	TEST_CASE("the retained fixture loads [save-format]")
	{
		glob2test::HeadlessGlobals globals;
	        FILE* file = std::fopen(glob2test::inflated("wrapped-building/reproducer.game.gz").string().c_str(), "rb");
	        require(file != nullptr, "open fixture");
	        GAGCore::BinaryInputStream reader(new GAGCore::FileStreamBackend(file));
	        GameGUI restoredGUI;
	        Game& loaded = restoredGUI.game;
	        require(loaded.load(&reader), "load wrapped-building fixture");
	        require(loaded.map.getW() == 32 && loaded.map.getH() == 32, "fixture dimensions");
	        require(loaded.mapHeader.getNumberOfTeams() == 1 && loaded.teams[0], "fixture team exists");
	        auto* building = loaded.teams[0]->myBuildings[0];
	        require(building != nullptr, "fixture building exists");
	        checkFootprint(loaded, *building);
	        int x, y, dx, dy;
	        require(building->findGroundExit(&x, &y, &dx, &dy, false), "fixture building exit");
	        require(loaded.map.getBuilding(x - dx, y - dy) == building->gid, "fixture exit adjacency");
	}
}
