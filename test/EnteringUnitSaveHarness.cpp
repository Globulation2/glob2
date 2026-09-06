// SPDX-License-Identifier: GPL-3.0-or-later
// Save during an explorer's final step into a building; retain corruption checks.
#include "GlobalContainer.h"
#include "Game.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void writeFixture(GAGCore::MemoryStreamBackend& bytes, const char* path)
{
    bytes.seekFromEnd(0);
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.getBuffer(), bytes.getPosition());
    output.close();
    require(!output.fail(), "write fixture file");
}

int main(int argc, char** argv)
{
    require(argc == 1 || (argc == 3 && (!std::strcmp(argv[1], "--load") ||
                                      !std::strcmp(argv[1], "--write-fixture"))),
            "usage: harness [--load FILE | --write-fixture FILE]");
    GlobalContainer globals;
    globalContainer = &globals;
    globals.runNoX = true;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    if (argc == 3 && !std::strcmp(argv[1], "--load"))
    {
        FILE* file = std::fopen(argv[2], "rb");
        require(file != nullptr, "open fixture");
        GAGCore::BinaryInputStream reader(new GAGCore::FileStreamBackend(file));
        Game loaded(nullptr);
        require(loaded.load(&reader), "load entering-explorer fixture");
        require(loaded.mapHeader.getNumberOfTeams() == 1 && loaded.teams[0], "fixture team exists");
        auto* unit = loaded.teams[0]->myUnits[0];
        require(unit && unit->typeNum == EXPLORER, "fixture explorer exists");
        require(unit->displacement == Unit::DIS_ENTERING_BUILDING, "fixture entry state");
        require(unit->posX == 8 && unit->posY == 8 && unit->dx == 1 && unit->dy == 0,
                "fixture destination and direction");
        require(loaded.map.getAirUnit(7, 8) == unit->gid, "fixture previous-tile occupancy");
        require(unit->attachedBuilding == loaded.teams[0]->myBuildings[0], "fixture entered building");
        std::puts("Entering-explorer saved fixture loaded with its previous-tile occupancy intact");
        return 0;
    }
    const int innType = globals.buildingsTypes.getTypeNum("inn", 0, false);
    const int positions[][2] = {{8,8}, {0,0}, {31,31}, {0,31}, {31,0}};
    int cases = 0;
    for (const auto& position : positions)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                if (!dx && !dy) continue;
                Game game(nullptr);
                game.map.setSize(5, 5, GRASS);
                game.map.setGame(&game);
                game.addTeam(0);
                auto* team = game.teams[0];
                const auto* type = globals.buildingsTypes.get(innType);
                const int bx = position[0] - (dx < 0 ? type->width - 1 : 0);
                const int by = position[1] - (dy < 0 ? type->height - 1 : 0);
                auto* inn = new Building(bx, by, 0, innType, team, &globals.buildingsTypes, 0, 0);
                team->myBuildings[0] = inn;
                game.map.setBuilding(bx, by, type->width, type->height, inn->gid);
                auto* unit = new Unit(position[0], position[1], 0, EXPLORER, team, 0);
                team->myUnits[0] = unit;
                unit->attachedBuilding = inn;
                inn->unitsInside.push_back(unit);
                unit->displacement = Unit::DIS_ENTERING_BUILDING;
                unit->dx = dx;
                unit->dy = dy;
                const int oldX = (position[0] - dx + 32) % 32;
                const int oldY = (position[1] - dy + 32) % 32;
                game.map.setAirUnit(oldX, oldY, unit->gid);
                require(game.integrity(), "entering explorer occupies its previous tile");

                auto* bytes = new GAGCore::MemoryStreamBackend;
                GAGCore::BinaryOutputStream writer(bytes);
                game.save(&writer, false, "entering explorer regression");
                if (argc == 3 && !std::strcmp(argv[1], "--write-fixture") &&
                    position[0] == 8 && position[1] == 8 && dx == 1 && dy == 0)
                    writeFixture(*bytes, argv[2]);
                auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
                copy->seekFromStart(0);
                GAGCore::BinaryInputStream reader(copy);
                Game loaded(nullptr);
                require(loaded.load(&reader), "save during building entry must load");
                auto* restored = loaded.teams[0]->myUnits[0];
                require(restored->posX == position[0] && restored->posY == position[1],
                        "save/load preserves animation destination");
                require(restored->attachedBuilding == loaded.teams[0]->myBuildings[0],
                        "save/load preserves the entered building reference");
                require(restored->dx == dx && restored->dy == dy, "save/load preserves entry direction");
                require(loaded.map.getAirUnit(oldX, oldY) == restored->gid,
                        "save/load preserves previous occupancy");

                // Invalid entering occupancy must still be rejected.
                loaded.map.setAirUnit(oldX, oldY, NOGUID);
                loaded.map.setAirUnit(position[0], position[1], restored->gid);
                require(!loaded.integrity(), "entering unit registered at destination is invalid");
                // Outside the entering state, use current position rather than old tile.
                restored->displacement = Unit::DIS_RANDOM;
                require(loaded.integrity(), "ordinary explorer occupies its current tile");
                loaded.map.setAirUnit(position[0], position[1], NOGUID);
                loaded.map.setAirUnit(oldX, oldY, restored->gid);
                require(!loaded.integrity(), "ordinary explorer at stale tile is invalid");
                ++cases;
            }
    std::printf("Entering unit save regressions passed: %d directions/positions and corruption controls\n", cases);
}
