// SPDX-License-Identifier: GPL-3.0-or-later
// Regression for wrapped footprints being erased by Game::load integrity repair.
#include "GlobalContainer.h"
#include "Game.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
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

int main()
{
    GlobalContainer globals;
    globalContainer = &globals;
    globals.runNoX = true;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    const int type = globals.buildingsTypes.getTypeNum("swarm", 0, false);
    const int positions[][2] = {{8,8}, {-1,8}, {8,-1}, {-1,-1}, {31,31}};
    for (const auto& position : positions)
    {
        Game game(nullptr);
        game.map.setSize(5, 5, GRASS);
        game.map.setGame(&game);
        game.addTeam(0);
        auto* team = game.teams[0];
        auto* building = new Building(position[0], position[1], 0, type, team,
                                      &globals.buildingsTypes, 0, 0);
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
        auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
        copy->seekFromStart(0);
        GAGCore::BinaryInputStream reader(copy);
        Game loaded(nullptr);
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
    std::puts("Building footprint regressions passed: repair, idempotence, save/load and exits at five positions");
}
