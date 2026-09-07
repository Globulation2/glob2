// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "WinningConditions.h"
#include "Utilities.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Colony
{
    GameGUI gui;
    Game& game = gui.game;
    Team* team;
    Building* inn;
    std::vector<Uint32> trace;

    Colony(int corn = 10)
    {
        setSyncRandSeed(110);
        game.map.setSize(5, 5, GRASS);
        game.map.setGame(&game);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                game.map.clearImmobileUnit(x, y);
        game.addTeam(0);
        team = game.teams[0];
        team->race.loadDefault();
        team->playersMask = 1;
        game.gameHeader.getWinningConditions().clear();
        game.gameHeader.getWinningConditions().push_back(std::make_shared<WinningConditionDeath>());
        const int type = globalContainer->buildingsTypes.getTypeNum("inn", 0, false);
        inn = new Building(8, 8, 0, type, team, &globalContainer->buildingsTypes, 0, 0);
        team->myBuildings[0] = inn;
        game.map.setBuilding(8, 8, inn->type->width, inn->type->height, inn->gid);
        inn->resources[CORN] = corn;
        inn->maxUnitInside = 1;
        inn->updateCallLists();
        game.map.setMapDiscovered();
    }

    Unit* unit(int type = WORKER)
    {
        auto* u = new Unit(6, 8, 0, type, team, 0);
        team->myUnits[0] = u;
        u->action = type == EXPLORER ? FLY : WALK;
        u->hungry = u->trigHungry / 2;
        u->medical = Unit::MED_HUNGRY;
        if (type == EXPLORER) game.map.setAirUnit(6, 8, u->gid);
        else game.map.setGroundUnit(6, 8, u->gid);
        return u;
    }

    void step(bool alive, const char* message)
    {
        team->syncStep();
        team->checkWinConditions();
        require(team->isAlive == alive && team->hasLost == !alive, message);
        trace.push_back(team->checkSum());
        ++game.stepCounter;
    }
};

static std::vector<Uint32> checkFeeding(int type, int corn)
{
    Colony c(corn);
    Unit* u = c.unit(type);
    // Complete an ordinary action: the simulation itself reserves the last meal.
    u->delta = 255;
    c.step(true, "reserving the final inn place must not cause defeat");
    require(u->attachedBuilding == c.inn && u->destinationPurpose == FEED,
            "hungry unit reserves a meal during the team step");
    require(c.team->canFeedUnit.empty() && c.team->canHealUnit.empty(),
            "full inn is absent from admission lists");
    require(u->insideTimeout == 0, "reservation precedes the feeding timer");
    bool entered = false;
    bool timerReachedZero = false;
    for (int tick = 0; tick < 2000 && u->hungry != Unit::HUNGRY_MAX; ++tick)
    {
        entered |= u->displacement == Unit::DIS_INSIDE;
        timerReachedZero |= u->displacement == Unit::DIS_INSIDE && u->insideTimeout == 0;
        c.step(true, "colony remains alive while approaching, entering and eating");
    }
    require(entered && timerReachedZero && u->hungry == Unit::HUNGRY_MAX,
            "unit completes its meal, including the zero timer boundary");
    require(c.inn->resources[CORN] == corn - 1, "one meal consumes exactly one corn");
    for (int tick = 0; tick < 300; ++tick)
        c.step(true, "fed colony remains alive through exit and medical refresh");
    require(u->attachedBuilding == nullptr && u->medical == Unit::MED_FREE,
            "fed unit exits and refreshes medical status");
    Uint32 digest = 2166136261u;
    for (Uint32 checksum : c.trace) digest = (digest ^ checksum) * 16777619u;
    std::printf("TRACE type=%d corn=%d seed=110 ticks=%zu digest=%08x\n", type, corn, c.trace.size(), unsigned(digest));
    return c.trace;
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    require(argc == 2 && std::string(argv[1]).find("glob2-hunger-test-") == 0,
            "run through test/run-hungry-defeat-tests.py");
    GlobalContainer globals(argv[1]);
    globalContainer = &globals;
    globals.runNoX = true;
    globals.settings.rememberUnit = false;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    for (int type : {WORKER, WARRIOR})
    for (int corn : {1, 10})
    {
        const auto first = checkFeeding(type, corn);
        const auto second = checkFeeding(type, corn);
        require(first == second, "identical seeds and fixtures reproduce every team checksum");
    }
    {
        Colony c;
        c.step(false, "an empty colony without a swarm still loses");
    }
    {
        Colony c;
        Unit* u = c.unit();
        u->medical = Unit::MED_FREE;
        u->hungry = Unit::HUNGRY_MAX;
        c.inn->resources[CORN] = 0;
        c.inn->updateCallLists();
        c.step(true, "a healthy worker without food still sustains a colony");
    }
    {
        Colony c;
        c.inn->resources[CORN] = 0;
        c.inn->updateCallLists();
        c.unit();
        c.step(false, "hungry colony with no food still loses");
    }
    {
        Colony c(0);
        Unit* u = c.unit();
        u->hungry = Unit::HUNGRY_MAX;
        u->hp = u->trigHP;
        c.step(false, "being fed does not override needing unavailable healing");
    }
    {
        Colony c;
        c.unit(EXPLORER)->delta = 255;
        c.step(false, "an explorer reservation alone does not sustain a colony");
    }
    {
        Colony c;
        c.unit()->delta = 255;
        c.team->playersMask = 0;
        c.step(false, "food reservations do not override lost player control");
    }
    std::puts("PASS: hunger defeat regression and defeat controls");
    return 0;
}
