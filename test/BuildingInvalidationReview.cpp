// SPDX-License-Identifier: GPL-3.0-or-later
// Review probe for PR #232: compare a cached building field after footprint
// changes with an explicitly invalidated, freshly rebuilt field.
// This probe asserts the known PR-head behavior; STALE is a correctness failure.
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "FileManager.h"
#include <SDL.h>
#include <string>
#include "Game.h"
#include "GameGUI.h"
#include "Unit.h"
#include "Team.h"
#include "MapInternal.h"
#include "Race.h"
#include "Ressource.h"
#include "IntBuildingType.h"
#include <cstdio>
#include <cstdlib>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void checkInvalidation(int obstacleX, bool removal, bool demolition=false)
{
    GameGUI gui;
    Game& game=gui.game;
    game.map.setSize(7,7,GRASS);
    game.map.setGame(&game);
    game.addTeam(0); game.addTeam(1);
    const int inn=globalContainer->buildingsTypes.getFinishedTypeNum("inn");
    auto* target=game.addBuilding(8,8,inn,0);
    require(target!=nullptr,"target inn created");
    Building* obstacle=nullptr;
    if (removal) obstacle=game.addBuilding(obstacleX,8,inn,1);
    require(!removal || obstacle!=nullptr,"obstacle inn created before removal");
    game.stepCounter=100;
    const auto index=game.map.coordToIndex(obstacleX,8);
    const auto* before=game.map.buildingGradient(target,0);
    require(before!=nullptr,"initial target field exists");
    const auto initial=before[index];
    require(removal ? initial==GRADIENT_FORBIDDEN : initial>GRADIENT_UNREACHABLE,
        "initial field matches initial footprint");
    if (removal) {
        if (demolition) { obstacle->launchDelete(); game.teams[1]->syncStep(); }
        else obstacle->kill();
    }
    else {
        obstacle=game.addBuilding(obstacleX,8,inn,1);
        require(obstacle!=nullptr,"new obstacle inn created");
        obstacle->owner->addToStaticAbilitiesLists(obstacle);
        obstacle->update();
    }
    const bool dirty=target->dirtyGradient[0];
    game.stepCounter=130;
    const auto* after=game.map.buildingGradient(target,0);
    require(after!=nullptr,"target field still exists");
    const auto cached=after[index];
    target->dirtyGradients();
    const auto* rebuilt=game.map.buildingGradient(target,0);
    require(rebuilt!=nullptr,"fresh target field exists");
    const auto fresh=rebuilt[index];
    require(removal ? fresh>GRADIENT_UNREACHABLE : fresh==GRADIENT_FORBIDDEN,
        "fresh field matches changed footprint");
    std::printf("%s obstacle x=%d: dirty=%d initial=%u cached=%u fresh=%u %s\n",
        demolition?"demolish":removal?"remove":"place",obstacleX,dirty,initial,cached,fresh,
        cached==fresh?"CORRECT":"STALE");
    require(removal && obstacleX==12 && !demolition ? cached==fresh : cached!=fresh,
        "review reproduction matches expected stale-field behavior");
}

int main(int argc, char** argv)
{
	SDL_SetMainReady();
	require(argc == 3, "usage: harness PROFILE ROOT");
	require(std::string(argv[1]).find("glob2-save-test-") == 0, "disposable profile required");
	GlobalContainer globals(argv[1]);
	globals.fileManager->addDir(argv[2]);
	globalContainer = &globals;
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	Race::loadDefault();
	checkInvalidation(12,false);
	checkInvalidation(12,true);
	checkInvalidation(64,true);
	checkInvalidation(12,true,true);
	return 0;
}
