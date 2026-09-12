// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "IntBuildingType.h"
#include "Unit.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
static void require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Fixture
{
    GameGUI gui;
    Game& game = gui.game;
    Fixture()
    {
        game.map.setSize(5, 5, GRASS);
        game.map.setGame(&game);
        game.addTeam(0);
        game.teams[0]->race.loadDefault();
        game.map.setMapDiscovered();
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) game.map.clearImmobileUnit(x, y);
    }
};

static GAGCore::MemoryStreamBackend encode(const Game& game, bool text)
{
    auto* backend = new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> stream;
    if (text) stream.reset(new GAGCore::TextOutputStream(backend));
    else stream.reset(new GAGCore::BinaryOutputStream(backend));
    game.saveBuildProjects(stream.get());
    stream->flush();
    return *backend;
}
static void decode(Game& game, GAGCore::MemoryStreamBackend bytes, bool text)
{
    bytes.seekFromStart(0);
    if (text) { GAGCore::TextInputStream stream(&bytes); game.loadBuildProjects(&stream); }
    else { GAGCore::BinaryInputStream stream(new GAGCore::MemoryStreamBackend(bytes)); game.loadBuildProjects(&stream); }
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    require(argc == 2, "use the disposable-profile runner");
    GlobalContainer globals(argv[1]); globalContainer = &globals;
    globals.runNoX = true; globals.settings.rememberUnit = false;
    globals.buildingsTypes.init(); IntBuildingType::init();
    const int type = globals.buildingsTypes.getTypeNum("inn", 0, true);
    for (bool text : {false, true})
    {
        Fixture source, target;
        source.game.buildProjects = {{8, 8, 0, type, 2, 3}, {16, 16, 0, type, 4, 5}};
        decode(target.game, encode(source.game, text), text);
        require(target.game.buildProjects.size() == 2, "both pending sites restored");
        auto first = target.game.buildProjects.begin(); auto second = std::next(first);
        require(first->posX == 8 && first->posY == 8 && first->teamNumber == 0 && first->typeNum == type
            && first->unitWorking == 2 && first->unitWorkingFuture == 3
            && second->posX == 16 && second->unitWorking == 4 && second->unitWorkingFuture == 5,
            "queue order and staffing survive both stream formats");
        // A moving unit still blocks the first site after reload. Clearing it
        // lets the saved request materialize without issuing a second order.
        target.game.map.setGroundUnit(8, 8, 0);
        target.game.buildProjectSyncStep(0);
        require(target.game.buildProjects.size() == 1, "blocked site stays pending while clear site starts");
        target.game.map.setGroundUnit(8, 8, NOGUID);
        target.game.buildProjectSyncStep(0);
        require(target.game.buildProjects.empty(), "restored site starts when its footprint clears");
        require(target.game.map.getBuilding(8, 8) != NOGBID, "pending building was created");
        source.game.buildProjects.clear();
        decode(target.game, encode(source.game, text), text);
        require(target.game.buildProjects.empty(), "empty queue round trip");
        source.game.buildProjects = {{8, 8, 99, type, 2, 3}};
        bool rejected = false;
        try { decode(target.game, encode(source.game, text), text); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected && target.game.buildProjects.empty(), "invalid reference rejected without partial restore");
    }
    std::puts("PASS: pending construction round trip and delayed placement");
}
