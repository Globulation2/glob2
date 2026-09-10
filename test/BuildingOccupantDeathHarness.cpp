// SPDX-License-Identifier: GPL-3.0-or-later
// A completed service does not put a unit on the map until an exit is found.
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Occupant : Unit
{
    using Unit::Unit;
    using Unit::handleMovementExitingBuilding;
    using Unit::handleAction;
};

static void destroyedDuringExit(int unitType, bool blocked)
{
    GameGUI gui;
    Game& game = gui.game;
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam(0);
    game.addTeam(1);
    game.teams[0]->race.loadDefault();
    game.teams[1]->race.loadDefault();
    auto* team = game.teams[0];
    const int innType = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
    auto* inn = game.addBuilding(8, 8, innType, 0);
    require(inn != nullptr, "create inn");
    auto* unit = new Occupant(8, 8, 0, unitType, team, 0);
    team->myUnits[0] = unit;
    unit->attachedBuilding = inn;
    unit->setTargetBuilding(inn);
    inn->unitsInside.push_back(unit);
    unit->activity = Unit::ACT_UPGRADING;
    unit->displacement = Unit::DIS_EXITING_BUILDING;
    unit->movement = Unit::MOV_INSIDE;
    unit->destinationPurpose = FEED;
    unit->needToRecheckMedical = true;
    const bool flying = unit->performance[FLY];
    std::vector<Unit*> blockers;
    if (blocked)
    {
        if (flying)
        {
            for (int x = 8; x < 8 + inn->type->width; ++x)
                for (int y = 8; y < 8 + inn->type->height; ++y)
                {
                    auto* blocker = game.addUnit(x, y, 1, EXPLORER, 0, 0, 0, 0);
                    require(blocker != nullptr, "occupy air exit");
                    blockers.push_back(blocker);
                }
        }
        else
        {
            for (int x = 7; x <= 8 + inn->type->width; ++x)
                for (int y = 7; y <= 8 + inn->type->height; ++y)
                {
                    if (x >= 8 && x < 8 + inn->type->width && y >= 8 && y < 8 + inn->type->height)
                        continue;
                    auto& resource = game.map.getResource(x, y);
                    resource.type = WOOD;
                    resource.amount = 1;
                }
        }
    }
    unit->handleMovementExitingBuilding();
    require(unit->movement == (blocked ? Unit::MOV_INSIDE : Unit::MOV_EXITING_BUILDING),
            "exit search reflects available space");
    unit->handleAction();
    inn->kill();
    require(bool(unit->isDead) == blocked, "destroyed inn kills trapped occupants but spares units that exited");
    require(unit->attachedBuilding == nullptr && inn->unitsInside.empty(), "destroyed inn releases subscriptions");
    if (blocked)
    {
        // The team must collect the dead unit before it can seek food and convert.
        team->syncStep();
        require(team->myUnits[0] == nullptr, "trapped occupant is collected on next team step");
        for (const auto* blocker : blockers)
            require(game.map.getAirUnit(blocker->posX, blocker->posY) == blocker->gid,
                    "indoor death preserves other units' map slots");
    }
    else
    {
        require((flying ? game.map.getAirUnit(unit->posX, unit->posY)
                        : game.map.getGroundUnit(unit->posX, unit->posY)) == unit->gid,
                "successful exit retains map occupancy");
    }
}

int main()
{
    GlobalContainer globals;
    globalContainer = &globals;
    globals.runNoX = true;
    globals.settings.rememberUnit = false;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    for (int type : {WORKER, EXPLORER})
        for (bool blocked : {true, false})
            destroyedDuringExit(type, blocked);
    std::puts("PASS: destroyed inns collect trapped ground/air occupants and preserve successful exits");
}
